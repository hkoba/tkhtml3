
/*
 * hv3see.c --
 *
 *     This file contains C-code that contributes to the Javascript based
 *     scripting environment in the Hv3 web browser. It assumes the
 *     availability of SEE (Simple EcmaScript Interpreter) and the Boehm
 *     C/C++ garbage collecting memory allocator.
 *
 *     SEE:      http://www.adaptive-enterprises.com.au/~d/software/see/
 *     Boehm GC: http://www.hpl.hp.com/personal/Hans_Boehm/gc/
 *
 *     Although it may be still be used for other purposes, the design
 *     of this extension is heavily influenced by the requirements of Hv3.
 *     It is not a generic javascript interpreter interface for Tcl.
 *
 * TODO: Copyright.
 */

/*------------------------------------------------------------------------
 * DESIGN NOTES
 *
 * TODO: Organize all this so that others can follow.
 *     
 * Javascript values:
 *
 *     undefined
 *     null
 *     boolean    BOOL
 *     number     NUMBER
 *     string     STRING
 *     object     COMMAND
 *     transient  COMMAND
 *
 * Interpreter Interface:
 *
 *     set interp [::see::interp]
 *
 *     $interp global TCL-OBJECT
 *
 *     $interp eval JAVASCRIPT
 *         Evaluate the supplied javascript using SEE_Global_eval().
 *
 *     $interp destroy
 *         Delete an interpreter. The command $interp is deleted by
 *         this command.
 *
 *     $interp tostring OBJ
 *         Return the string form of a javascript object.
 *
 *     $interp eventtarget
 *         Create a new event-target object.
 *
 * Object Interface:
 *
 *         $obj Call         THIS ?ARG-VALUE...?
 *         $obj CanPut       PROPERTY
 *         $obj Construct    ?ARG-VALUE...?
 *         $obj DefaultValue
 *         $obj Delete       PROPERTY
 *         $obj Enumerator
 *         $obj Get          PROPERTY
 *         $obj HasProperty  PROPERTY
 *         $obj Put          PROPERTY ARG-VALUE
 *
 *     Argument PROPERTY is a simple property name (i.e. "className"). VALUE 
 *     is a typed javascript value. 
 *
 * Object resource management:
 *
 *     All based around the following interface:
 *
 *         $obj Finalize
 *
 *     1. The global object.
 *     2. Tcl-command based objects.
 *
 *     1. There are no resource management issues on the global object.
 *        The script must ensure that the specified command exists for the
 *        lifetime of the interpreter. The [Finalize] method is never
 *        called by the extension on the global object (not even when the
 *        interpreter is destroyed).
 *
 *     2. Objects may be created by Tcl scripts by returning a value of the
 *        form {object COMMAND} from a call to the [Call], [Construct] or
 *        [Get] method of the "Object Interface" (see above). Assuming 
 *        that COMMAND is a Tcl command, the "COMMAND" values is
 *        transformed to a javascript value containing an object 
 *        reference as follows:
 *
 *            i. Search the SeeInterp.aTclObject table for a match. If
 *               found, use the SeeTclObject* as the (struct SEE_object *)
 *               value to pass to javascript.
 *
 *           ii. If not found, allocate and populate a new SeeTclObject
 *               structure. Insert it into the aTclObject[] table. It is
 *               allocated with SEE_NEW_FINALIZE(). The finalization
 *               callback:
 *               
 *               a) Removes the entry from athe aTclObject[] table, and
 *               b) Calls the [Finalize] method of the Tcl command.
 *
 *        If the Tcl script wants to delete the underlying Tcl object from
 *        within the [Finalize] method, it may safely do so.
 */

#include <tcl.h>
#include <see/see.h>

#ifdef NO_HAVE_GC
    /* If the symbol NO_HAVE_GC is defined, have SEE use regular malloc() 
     * instead of a garbage-collecting version. Of course, it leaks a
     * lot of memory when compiled this way.
     */
    #define GC_MALLOC_UNCOLLECTABLE(x) ckalloc(x)
    #define GC_FREE(x) ckfree((char *)x)
    #define GC_register_finalizer(a,b,c,d,e)
#else
    #include <gc.h>
#endif

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>

#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

typedef struct SeeInterp SeeInterp;
typedef struct SeeTclObject SeeTclObject;
typedef struct SeeJsObject SeeJsObject;

#define OBJECT_HASH_SIZE 257

struct SeeInterp {
    /* The two interpreters - SEE and Tcl. */
    struct SEE_interpreter interp;
    Tcl_Interp *pTclInterp;

    /* Hash table containing the objects created by the Tcl interpreter.
     * This maps from the Tcl command to the SeeTclObject structure.
     */
    SeeTclObject *aTclObject[OBJECT_HASH_SIZE];

    /* Tcl name of the global object. The NULL-terminated string is
     * allocated using SEE_malloc_string() when the global object
     * is set (the [$interp global] command).
     */
    char *zGlobal;

    /* Linked list of SeeJsObject structures that will be removed from
     * the aJsObject[] table next time removeTransientRefs() is called.
     */
    int iNextJsObject;
    SeeJsObject *pJsObject;
};
static int iSeeInterp = 0;


/* Each javascript object created by the Tcl-side is represented by
 * an instance of the following struct.
 */
struct SeeTclObject {
    struct SEE_native native;   /* Base class - store native properties here */
    SeeInterp *pTclSeeInterp;   /* Pointer to the owner ::see::interp */
    Tcl_Obj *pObj;              /* Tcl script for object */

    /* Next entry (if any) in the SeeInterp.aObject hash table */
    SeeTclObject *pNext;
};

/* Entries in the SeeInterp.pJsObject[] linked list are instances of
 * the following structure.
 */
struct SeeJsObject {
    int iKey;
    struct SEE_object *pObject;

    /* Next entry in the SeeInterp.pJsObject list */
    SeeJsObject *pNext;
};

/*
 * Forward declarations for the event-target implementation.
 */
static Tcl_ObjCmdProc    eventTargetNew;
static Tcl_ObjCmdProc    eventTargetMethod;
static Tcl_CmdDeleteProc eventTargetDelete;
static struct SEE_object *eventTargetValue(SeeInterp *, Tcl_Obj *, Tcl_Obj *);

static void installHv3Global(SeeInterp *, struct SEE_object *);

/* Return a pointer to the V-Table for Tcl based javascript objects. */
static struct SEE_objectclass *getVtbl();


/*
 *---------------------------------------------------------------------------
 *
 * hashCommand --
 *     Return a hash value between 0 and (OBJECT_HASH_SIZE-1) for the
 *     nul-terminated string passed as an argument.
 *
 * Results: 
 *     Integer between 0 and (OBJECT_HASH_SIZE-1), inclusive.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static int
hashCommand(zCommand)
    char const *zCommand;
{
    unsigned int iSlot = 0;
    char const *z;
    for (z = zCommand ; *z; z++) {
        iSlot = (iSlot << 3) + (int)(*z);
    }
    return (iSlot % OBJECT_HASH_SIZE);
}

/*
 *---------------------------------------------------------------------------
 *
 * createObjectRef --
 *     Insert an entry in the SeeInterp.aJsObject[] table for pObject.
 *     return the integer key associated with the table entry.
 *
 * Results: 
 *     Integer.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static int
createObjectRef(pTclSeeInterp, pObject)
    SeeInterp *pTclSeeInterp;
    struct SEE_object *pObject;
{
    SeeJsObject *pJsObject;

    /* Create the new SeeJsObject structure. */
    pJsObject = SEE_NEW(&pTclSeeInterp->interp, SeeJsObject);
    pJsObject->iKey = pTclSeeInterp->iNextJsObject++;
    pJsObject->pObject = pObject;

    pJsObject->pNext = pTclSeeInterp->pJsObject;
    pTclSeeInterp->pJsObject = pJsObject;

    return pJsObject->iKey;
}

/*
 *---------------------------------------------------------------------------
 *
 * removeTransientRefs --
 *
 * Results: 
 *     None.
 *
 * Side effects:
 *
 *---------------------------------------------------------------------------
 */
static void
removeTransientRefs(pTclSeeInterp, n)
    SeeInterp *pTclSeeInterp;
    int n;
{
    int ii;
    for(ii = 0; ii < n; ii++) {
        pTclSeeInterp->pJsObject = pTclSeeInterp->pJsObject->pNext;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * lookupObjectRef --
 *     Lookup the entry associated with parameter iKey in the 
 *     SeeInterp.pJsObject list. Return a pointer to the SEE object
 *     stored as part of the entry.
 *
 *     If there is no such entry in the SeeInterp.pJsObject list,
 *     return NULL and leave an error in the Tcl interpreter.
 *
 * Results: 
 *     Pointer to SEE_object, or NULL.
 *
 * Side effects:
 *     May write an error to SeeInterp.pTclInterp.
 *
 *---------------------------------------------------------------------------
 */
static struct SEE_object *
lookupObjectRef(pTclSeeInterp, iKey)
    SeeInterp *pTclSeeInterp;
    int iKey;
{
    SeeJsObject *pJsObject;

    for (
        pJsObject = pTclSeeInterp->pJsObject;
        pJsObject && pJsObject->iKey != iKey;
        pJsObject = pJsObject->pNext
    );

    if (!pJsObject) {
        char zBuf[64];
        sprintf(zBuf, "No such object: %d", iKey);
        Tcl_SetResult(pTclSeeInterp->pTclInterp, zBuf, TCL_VOLATILE);
        return 0;
    }

    return pJsObject->pObject;
}

/*
 *---------------------------------------------------------------------------
 *
 * primitiveValueToTcl --
 *
 *     Convert the SEE value *pValue to it's Tcl representation, assuming
 *     that *pValue holds a primitive value, not a javascript object. 
 *     Return a pointer to a new Tcl object (ref-count 0) containing
 *     the Tcl representation.
 *
 *     If *pValue does contain a javascript object, use SEE_ToPrimitive()
 *     to convert it to a primitive. The conversion is done on a copy
 *     to *pValue, so *pValue is never modified.
 *
 * Results:
 *     Tcl object with ref-count set to 0.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static Tcl_Obj *
primitiveValueToTcl(pTclSeeInterp, pValue)
    SeeInterp *pTclSeeInterp;
    struct SEE_value *pValue;
{
    Tcl_Obj *aTclValues[2] = {0, 0};
    int nTclValues = 2;
    struct SEE_value copy;
    struct SEE_value *p = pValue;

    if (SEE_VALUE_GET_TYPE(pValue) == SEE_OBJECT) {
        SEE_ToPrimitive(&pTclSeeInterp->interp, pValue, 0, &copy);
        p = &copy;
    }

    switch (SEE_VALUE_GET_TYPE(p)) {

        case SEE_UNDEFINED:
            aTclValues[0] = Tcl_NewStringObj("undefined", -1);
            break;

        case SEE_NULL:
            aTclValues[0] = Tcl_NewStringObj("null", -1);
            break;

        case SEE_BOOLEAN:
            aTclValues[0] = Tcl_NewStringObj("boolean", -1);
            aTclValues[1] = Tcl_NewBooleanObj(pValue->u.boolean);
            break;

        case SEE_NUMBER:
            aTclValues[0] = Tcl_NewStringObj("number", -1);
            aTclValues[1] = Tcl_NewDoubleObj(pValue->u.number);
            break;

        case SEE_STRING:
            aTclValues[0] = Tcl_NewStringObj("string", -1);
            aTclValues[1] = Tcl_NewUnicodeObj(
                pValue->u.string->data, pValue->u.string->length
            );
            break;

        case SEE_OBJECT: 
        default:
            assert(!"Bad value type");

    }

    assert(aTclValues[0]);
    if (!aTclValues[1]) {
        nTclValues = 1;
    }

    return Tcl_NewListObj(nTclValues, aTclValues);
}

/*
 *---------------------------------------------------------------------------
 *
 * argValueToTcl --
 *
 *     Convert the SEE value *pValue to it's Tcl form. If *pValue contains
 *     a primitive (non javascript-object) value, use primitiveValueToTcl()
 *     to convert it. 
 *
 *     If *pValue contains an object reference, then add an entry for the
 *     object to the SeeInterp.aJsObject[] array and return an object
 *     reference of the form:
 *
 *         {object {INTERP Get ID}}
 *
 *     The ID for the new object reference is added to the
 *     SeeInterp.pTransient list (so that it is removed by
 *     removeTransientRefs()).
 *
 * Results:
 *     Tcl object with ref-count set to 0.
 *
 * Side effects:
 *     May add an entry to SeeInterp.aJsObject[].
 *
 *---------------------------------------------------------------------------
 */
static Tcl_Obj *
argValueToTcl(pTclSeeInterp, pValue, piObject)
    SeeInterp *pTclSeeInterp;
    struct SEE_value *pValue;
    int *piObject;
{
    if (SEE_VALUE_GET_TYPE(pValue) == SEE_OBJECT) {
        int iKey;
        Tcl_Obj *aTclValues[2];
        struct SEE_object *pObject = pValue->u.object;

        aTclValues[0] = Tcl_NewStringObj("object", -1);

        if (pObject->objectclass == getVtbl()) {
          aTclValues[1] = ((SeeTclObject *)pObject)->pObj;
        } else {
          iKey = createObjectRef(pTclSeeInterp, pObject, 1);
          aTclValues[1] = Tcl_NewIntObj(iKey);
          (*piObject)++;
        }
        return Tcl_NewListObj(2, aTclValues);
    } else {
        return primitiveValueToTcl(pTclSeeInterp, pValue);
    }
}

static SeeTclObject *
newSeeTclObject(pTclSeeInterp, pTclCommand)
    SeeInterp *pTclSeeInterp;
    Tcl_Obj *pTclCommand;
{
    SeeTclObject *p;

    p = SEE_NEW(&pTclSeeInterp->interp, SeeTclObject);
    p->pTclSeeInterp = pTclSeeInterp;
    p->pObj = pTclCommand;
    p->pNext = 0;
    Tcl_IncrRefCount(p->pObj);
    SEE_native_init(
            &p->native,
            &pTclSeeInterp->interp, 
            getVtbl(),
            pTclSeeInterp->interp.Object_prototype       
    );

    return p;
}

/*
 *---------------------------------------------------------------------------
 *
 * finalizeTransient --
 *
 * Results:
 *     Do final cleanup on a transient Tcl object.
 *
 * Side effects:
 *
 *---------------------------------------------------------------------------
 */
static void
finalizeTransient(pPtr, pContext)
    void *pPtr;
    void *pContext;
{
    Tcl_Obj *pFinalize;
    SeeTclObject *p = (SeeTclObject *)pPtr;
    SeeInterp *pTclSeeInterp = (SeeInterp *)pContext;

    /* Execute the Tcl Finalize hook. Do nothing with the result thereof. */
    pFinalize = Tcl_DuplicateObj(p->pObj);
    Tcl_IncrRefCount(pFinalize);
    Tcl_ListObjAppendElement(0, pFinalize, Tcl_NewStringObj("Finalize", 8));
    Tcl_EvalObjEx(pTclSeeInterp->pTclInterp, pFinalize, TCL_GLOBAL_ONLY);
    Tcl_DecrRefCount(pFinalize);

    /* Decrement the ref count on the Tcl object */
    Tcl_DecrRefCount(p->pObj);
}

static void
finalizeObject(pPtr, pContext)
    void *pPtr;
    void *pContext;
{
    SeeTclObject *p = (SeeTclObject *)pPtr;
    Tcl_DecrRefCount(p->pObj);
}

/*
 *---------------------------------------------------------------------------
 *
 * createTransient --
 *
 * Results:
 *     Pointer to SEE_object structure.
 *
 * Side effects:
 *
 *---------------------------------------------------------------------------
 */
static struct SEE_object *
createTransient(pTclSeeInterp, pTclCommand)
    SeeInterp *pTclSeeInterp;
    Tcl_Obj *pTclCommand;
{
    SeeTclObject *p = newSeeTclObject(pTclSeeInterp, pTclCommand);
    GC_register_finalizer(p, finalizeTransient, pTclSeeInterp, 0, 0);
    return (struct SEE_object *)p;
}

/*
 *---------------------------------------------------------------------------
 *
 * findOrCreateObject --
 *
 * Results:
 *     Pointer to SEE_object structure.
 *
 * Side effects:
 *     May create a new SeeTclObject structure and add it to the
 *     SeeInterp.aObject hash table.
 *
 *---------------------------------------------------------------------------
 */
static struct SEE_object *
findOrCreateObject(pTclSeeInterp, pTclCommand)
    SeeInterp *pTclSeeInterp;
    Tcl_Obj *pTclCommand;
{
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    char const *zCommand = Tcl_GetString(pTclCommand);
    int iSlot = hashCommand(zCommand);
    SeeTclObject *pObject;

    /* Check for the global object */
    if (pTclSeeInterp->zGlobal && !strcmp(zCommand, pTclSeeInterp->zGlobal)) {
        return pTclSeeInterp->interp.Global;
    }

    /* See if this is a javascript object reference. It is assumed to
     * be a javascript reference if the first character is a digit.
     */
    if ( isdigit((unsigned char)zCommand[0]) ){
        int iKey;
        if (TCL_OK != Tcl_GetIntFromObj(pTclInterp, pTclCommand, &iKey)) {
            return 0;
        }
        return lookupObjectRef(pTclSeeInterp, iKey);
    }

    /* Search for an existing Tcl object */
    for (
        pObject = pTclSeeInterp->aTclObject[iSlot];
        pObject && strcmp(zCommand, Tcl_GetString(pObject->pObj));
        pObject = pObject->pNext
    );

    /* If pObject is still NULL, there is no existing object, create a new
     * SeeTclObject.
     */
    if (!pObject) {
        pObject = newSeeTclObject(pTclSeeInterp, pTclCommand);
        GC_register_finalizer(pObject, finalizeObject, 0, 0, 0);

        /* Insert the new object into the hash table */
        pObject->pNext = pTclSeeInterp->aTclObject[iSlot];
        pTclSeeInterp->aTclObject[iSlot] = pObject;
    }

    return (struct SEE_object *)pObject;
}

static int
objToValue(pInterp, pObj, pValue)
    SeeInterp *pInterp;
    Tcl_Obj *pObj;                  /* IN: Tcl js value */
    struct SEE_value *pValue;       /* OUT: Parsed value */
{
    int rc;
    int nElem = 0;
    Tcl_Obj **apElem = 0;

    Tcl_Interp *pTclInterp = pInterp->pTclInterp;

    rc = Tcl_ListObjGetElements(pTclInterp, pObj, &nElem, &apElem);
    if (rc == TCL_OK) {
        assert(nElem == 0 || 0 != strcmp("", Tcl_GetString(pObj)));

        if (nElem == 0) {
            SEE_SET_UNDEFINED(pValue);
        } else {
            int iChoice;
            #define EVENT_VALUE -123
            #define TRANSIENT -124
            struct ValueType {
                char const *zType;
                int eType;
                int nArg;
            } aType[] = {
                {"undefined", SEE_UNDEFINED, 0}, 
                {"null",      SEE_NULL, 0}, 
                {"number",    SEE_NUMBER, 1}, 
                {"string",    SEE_STRING, 1}, 
                {"boolean",   SEE_BOOLEAN, 1},
                {"object",    SEE_OBJECT, 1},
                {"transient", TRANSIENT, 1},
                {"event",     EVENT_VALUE, 2},
                {0, 0, 0}
            };

            if (Tcl_GetIndexFromObjStruct(pTclInterp, apElem[0], aType,
                sizeof(struct ValueType), "type", 0, &iChoice) 
            ){
                Tcl_AppendResult(pTclInterp, 
                    " value was \"", Tcl_GetString(pObj), "\"", 0
                );
                return TCL_ERROR;
            }
            if (nElem != (aType[iChoice].nArg + 1)) {
                Tcl_AppendResult(pTclInterp, 
                    "Bad javascript value spec: \"", Tcl_GetString(pObj),
                    "\"", 0
                );
                return TCL_ERROR;
            }
            switch (aType[iChoice].eType) {
                case SEE_UNDEFINED:
                    SEE_SET_UNDEFINED(pValue);
                    break;
                case SEE_NULL:
                    SEE_SET_NULL(pValue);
                    break;
                case SEE_BOOLEAN: {
                    int val;
                    if (Tcl_GetBooleanFromObj(pTclInterp, apElem[1], &val)) {
                        return TCL_ERROR;
                    }
                    SEE_SET_BOOLEAN(pValue, val);
                    break;
                }
                case SEE_NUMBER: {
                    double val;
                    const char *zElem = Tcl_GetString(apElem[1]);
                    if (0==strcmp(zElem, "-NaN") || 0==strcmp(zElem, "NaN")) {
                        struct SEE_value v;
                        struct SEE_string *pNaN = SEE_intern_ascii(
                            &pInterp->interp, "NaN"
                        );
                        SEE_SET_STRING(&v, pNaN);
                        SEE_ToNumber(&pInterp->interp, &v, pValue);
                    } else 
                    if (Tcl_GetDoubleFromObj(pTclInterp, apElem[1], &val)) {
                        return TCL_ERROR;
                    } else {
                        SEE_SET_NUMBER(pValue, val);
                    }
                    break;
                }
                case SEE_STRING: {
                    int nChar;
                    Tcl_UniChar *zChar;
                    struct SEE_string *pString;
                    struct SEE_string str;

                    zChar = Tcl_GetUnicodeFromObj(apElem[1], &nChar);;

                    pString = SEE_string_new(&pInterp->interp, nChar);
                    str.length = nChar;
                    str.data = zChar;
                    SEE_string_append(pString, &str);

                    SEE_SET_STRING(pValue, pString);
                    break;
                }

                case SEE_OBJECT: {
                    struct SEE_object *pObject = 
                        findOrCreateObject(pInterp, apElem[1]);
                    SEE_SET_OBJECT(pValue, pObject);
                    break;
                }

                case TRANSIENT: {
                    struct SEE_object *pObject = 
                        createTransient(pInterp, apElem[1]);
                    SEE_SET_OBJECT(pValue, pObject);
                    break;
                }

                case EVENT_VALUE: {
                    struct SEE_object *pObject = 
                        eventTargetValue(pInterp, apElem[0], apElem[1]);
                    if (pObject) {
                        SEE_SET_OBJECT(pValue, pObject);
                    } else {
                        SEE_SET_UNDEFINED(pValue);
                    }
                    break;
                }
            }
        }
    }
    return rc;
}

struct SEE_string *
SEE_function_getname(struct SEE_interpreter * i, struct SEE_object *o);

/*
 *---------------------------------------------------------------------------
 *
 * handleJavascriptError --
 *
 *     This function is designed to be called when a javascript error
 *     occurs (i.e. a SEE exception is thrown by either SEE_Global_eval()
 *     or SEE_OBJECT_CALL()).
 *
 *     Information is retrieved from the try-catch context passed as 
 *     argument pTry and loaded into the result of the Tcl-interpreter
 *     component of argument pTclSeeInterp. 
 *
 * Results:
 *     Always returns TCL_ERROR.
 *
 * Side effects:
 *     Sets the result of Tcl interpreter pTclSeeInterp->pTclInterp.
 *
 *---------------------------------------------------------------------------
 */
static int 
handleJavascriptError(pTclSeeInterp, pTry)
    SeeInterp *pTclSeeInterp;
    SEE_try_context_t *pTry;
{
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    struct SEE_interpreter *pSeeInterp = &pTclSeeInterp->interp;

    struct SEE_traceback *pTrace;

    struct SEE_value error;
    Tcl_Obj *pError = Tcl_NewStringObj("Javascript Error: ", -1);

    SEE_ToString(pSeeInterp, SEE_CAUGHT(*pTry), &error);
    if (SEE_VALUE_GET_TYPE(&error) == SEE_STRING) {
        struct SEE_string *pS = error.u.string;
        Tcl_AppendUnicodeToObj(pError, pS->data, pS->length);
    } else {
        Tcl_AppendToObj(pError, "unknown.", -1);
    }
    Tcl_AppendToObj(pError, "\n\n", -1);

    for (pTrace = pTry->traceback; pTrace; pTrace = pTrace->prev) {
        struct SEE_string *pLocation;

        pLocation = SEE_location_string(pSeeInterp, pTrace->call_location);
        Tcl_AppendUnicodeToObj(pError, pLocation->data, pLocation->length);
        Tcl_AppendToObj(pError, "  ", -1);

        switch (pTrace->call_type) {
            case SEE_CALLTYPE_CONSTRUCT: {
                char const *zClass =  pTrace->callee->objectclass->Class;
                if (!zClass) zClass = "?";
                Tcl_AppendToObj(pError, "new ", -1);
                Tcl_AppendToObj(pError, zClass, -1);
                break;
            }
            case SEE_CALLTYPE_CALL: {
                struct SEE_string *pName;
                Tcl_AppendToObj(pError, "call ", -1);
                pName = SEE_function_getname(pSeeInterp, pTrace->callee);
                if (pName) {
                    Tcl_AppendUnicodeToObj(pError, pName->data, pName->length);
                } else {
                    Tcl_AppendToObj(pError, "?", -1);
                }
                break;
            }
            default:
                assert(0);
        }

        Tcl_AppendToObj(pError, "\n", -1);
    }

    Tcl_SetObjResult(pTclInterp, pError);
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * delInterpCmd --
 *
 *     This function is called when a SeeInterp is deleted.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *
 *---------------------------------------------------------------------------
 */
static void 
delInterpCmd(clientData)
    ClientData clientData;             /* The SeeInterp data structure */
{
    SeeInterp *pTclSeeInterp = (SeeInterp *)clientData;
    SEE_gcollect(&pTclSeeInterp->interp);
    GC_FREE(pTclSeeInterp);
}

/*
 *---------------------------------------------------------------------------
 *
 * interpEval --
 *
 *     This function does the work of the [$see_interp eval JAVASCRIPT]
 *     Tcl command.
 *
 * Results:
 *     Tcl result - TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *     Executes the block of javascript passed as parameter pCode in
 *     the global context.
 *
 *---------------------------------------------------------------------------
 */
static int
interpEval(pTclSeeInterp, pCode, pFile)
    SeeInterp *pTclSeeInterp;     /* Interpreter */
    Tcl_Obj *pCode;               /* Javascript to evaluate */
    Tcl_Obj *pFile;               /* File-name for stack-traces (may be NULL) */
{
    struct SEE_interpreter *pSeeInterp = &(pTclSeeInterp->interp);
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    struct SEE_input *pInputCode;
    int rc = TCL_OK;
    SEE_try_context_t try_ctxt;

    Tcl_ResetResult(pTclInterp);

    pInputCode = SEE_input_utf8(pSeeInterp, Tcl_GetString(pCode));
    if( pFile ){
        pInputCode->filename = SEE_string_sprintf(
            pSeeInterp, "%s", Tcl_GetString(pFile)
        );
    }
    SEE_TRY(pSeeInterp, try_ctxt) {
        struct SEE_value res;
        SEE_Global_eval(pSeeInterp, pInputCode, &res);
        Tcl_SetObjResult(pTclInterp, primitiveValueToTcl(pTclSeeInterp, &res));
    }
    SEE_INPUT_CLOSE(pInputCode);

    if (SEE_CAUGHT(try_ctxt)) {
        rc = handleJavascriptError(pTclSeeInterp, &try_ctxt);
    }

    return rc;
}

/*
 *---------------------------------------------------------------------------
 *
 * interpCmd --
 *
 *       $interp eval JAVASCRIPT
 *       $interp destroy
 *       $interp function JAVASCRIPT
 *       $interp native
 *       $interp global COMMAND
 * 
 *       $interp Get ID ?OBJECT-CMD...?
 *
 * Results:
 *
 * Side effects:
 *
 *---------------------------------------------------------------------------
 */
static int 
interpCmd(clientData, pTclInterp, objc, objv)
    ClientData clientData;             /* The SeeInterp data structure */
    Tcl_Interp *pTclInterp;            /* Current interpreter. */
    int objc;                          /* Number of arguments. */
    Tcl_Obj *CONST objv[];             /* Argument strings. */
{
    int rc = TCL_OK;
    int iChoice;
    SeeInterp *pTclSeeInterp = (SeeInterp *)clientData;
    struct SEE_interpreter *pSeeInterp = &pTclSeeInterp->interp;

    enum INTERP_enum {
        INTERP_EVAL,
        INTERP_DESTROY,
        INTERP_GLOBAL,
        INTERP_TOSTRING,
        INTERP_EVENTTARGET,
    };

    static const struct InterpSubCommand {
        const char *zCommand;
        enum INTERP_enum eSymbol;
        int nMinArgs;
        int nMaxArgs;
        char *zArgs;
    } aSubCommand[] = {
        {"destroy",     INTERP_DESTROY, 0, 0, ""},
        {"eval",        INTERP_EVAL,    1, 3, "?-file FILENAME? JAVASCRIPT"},
        {"eventtarget", INTERP_EVENTTARGET, 0, 0, ""},
        {"global",      INTERP_GLOBAL,      1, 1, "TCL-COMMAND"},
        {"tostring",    INTERP_TOSTRING,    1, 1, "JAVASCRIPT-VALUE"},
        {0, 0, 0, 0}
    };

    if (objc<2) {
        Tcl_WrongNumArgs(pTclInterp, 1, objv, "SUBCOMMAND ...");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(pTclInterp, objv[1], aSubCommand, 
            sizeof(struct InterpSubCommand), "option", 0, &iChoice) 
    ){
        return TCL_ERROR;
    }

    if (
        objc < (aSubCommand[iChoice].nMinArgs + 2) || 
        objc > (aSubCommand[iChoice].nMaxArgs + 2)
    ) {
        Tcl_WrongNumArgs(pTclInterp, 2, objv, aSubCommand[iChoice].zArgs);
        return TCL_ERROR;
    }

    switch (aSubCommand[iChoice].eSymbol) {

        /*
         * seeInterp eval ?-file FILENAME? PROGRAM-TEXT
         * 
         *     Evaluate a javascript script in the global context.
         */
        case INTERP_EVAL: {
            Tcl_Obj *pFile = 0;
            if( objc == 5 ){
                pFile = objv[3];
            }
            rc = interpEval(pTclSeeInterp, objv[objc-1], pFile);
            break;
        }

        /*
         * seeInterp global TCL-COMMAND
         *
         */
        case INTERP_GLOBAL: {
            int nGlobal;
            char *zGlobal;
            struct SEE_object *pWindow;

            if (pTclSeeInterp->zGlobal) {
                Tcl_ResetResult(pTclInterp);
                Tcl_AppendResult(pTclInterp, "Can call [global] only once.", 0);
                return TCL_ERROR;
            }
            pWindow = findOrCreateObject(pTclSeeInterp, objv[2]);
            installHv3Global(pTclSeeInterp, pWindow);

            zGlobal = Tcl_GetStringFromObj(objv[2], &nGlobal);
            pTclSeeInterp->zGlobal = SEE_malloc_string(pSeeInterp, nGlobal + 1);
            strcpy(pTclSeeInterp->zGlobal, zGlobal);
            break;
        }

        /*
         * seeInterp destroy
         *
         */
        case INTERP_DESTROY: {
            Tcl_DeleteCommand(pTclInterp, Tcl_GetString(objv[0]));
            break;
        }

        /*
         * $interp tostring VALUE
         */
        case INTERP_TOSTRING: {
            struct SEE_value val;
            struct SEE_value res;
            objToValue(pTclSeeInterp, objv[2], &val);
            SEE_ToString(pSeeInterp, &val, &res);
            Tcl_SetObjResult(pTclInterp, Tcl_NewUnicodeObj(
                res.u.string->data, res.u.string->length
            ));
            break;
        }

        /*
         * $interp eventtarget
         */
        case INTERP_EVENTTARGET: {
            return eventTargetNew(clientData, pTclInterp, objc, objv);
        }
    }

    return rc;
}

/*
 *---------------------------------------------------------------------------
 *
 * tclSeeInterp --
 *
 *     Implementation of [::see::interp].
 *
 *     Creates a new javascript interpreter object-command.
 *
 * Results:
 *     TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *     See above.
 *
 *---------------------------------------------------------------------------
 */
static int 
tclSeeInterp(clientData, interp, objc, objv)
    ClientData clientData;             /* Unused */
    Tcl_Interp *interp;                /* Current interpreter. */
    int objc;                          /* Number of arguments. */
    Tcl_Obj *CONST objv[];             /* Argument strings. */
{
    char zCmd[64];
    SeeInterp *pInterp;

    sprintf(zCmd, "::see::interp_%d", iSeeInterp++);

    pInterp = (SeeInterp *)GC_MALLOC_UNCOLLECTABLE(sizeof(SeeInterp));
    memset(pInterp, 0, sizeof(SeeInterp));
 
    /* Initialize a new SEE interpreter */
    SEE_interpreter_init_compat(&pInterp->interp, 
        SEE_COMPAT_JS15|SEE_COMPAT_SGMLCOM
    );

    /* Initialise the pTclInterp field. */
    pInterp->pTclInterp = interp;

    Tcl_CreateObjCommand(interp, zCmd, interpCmd, pInterp, delInterpCmd);
    Tcl_SetResult(interp, zCmd, TCL_VOLATILE);
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * stringToObj --
 *
 *     Create a Tcl object containing a copy of the string pString. The
 *     returned object has a ref-count of 0.
 *
 * Results:
 *
 * Side effects:
 *
 *---------------------------------------------------------------------------
 */
static Tcl_Obj *
stringToObj(pString)
    struct SEE_string *pString;
{
    return Tcl_NewUnicodeObj(pString->data, pString->length);
}


static void 
SeeTcl_Get(pInterp, pObj, pProp, pRes)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
    struct SEE_value *pRes;
{
    SeeTclObject *pObject = (SeeTclObject *)pObj;
    SeeInterp *pTclSeeInterp = pObject->pTclSeeInterp;
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    Tcl_Obj *pScript = Tcl_DuplicateObj(pObject->pObj);

    Tcl_Obj *pScriptRes;
    int nRes;
    int rc;

    /* Execute the script:
     *
     *     $obj Get $property
     */
    Tcl_IncrRefCount(pScript);
    Tcl_ListObjAppendElement(pTclInterp, pScript, Tcl_NewStringObj("Get", 3));
    Tcl_ListObjAppendElement(pTclInterp, pScript, stringToObj(pProp));
    rc = Tcl_EvalObjEx(pTclInterp, pScript, TCL_GLOBAL_ONLY);
    Tcl_DecrRefCount(pScript);

    if (rc == TCL_OK) {
        pScriptRes = Tcl_GetObjResult(pTclInterp);
        rc = Tcl_ListObjLength(pTclInterp, pScriptRes, &nRes);
    }

    if (rc == TCL_OK) {
        if (nRes == 0) {
            /* If the [$obj Get] script returned a list of zero length (i.e.
             * an empty string), then look up the property in the 
             * pObject->native hash table.
             */
            struct SEE_object *pNative = (struct SEE_object*)(&pObject->native);
            SEE_native_get(pInterp, pNative, pProp, pRes);
        } else {
            Tcl_IncrRefCount(pScriptRes);
            rc = objToValue(pTclSeeInterp, pScriptRes, pRes);
            Tcl_DecrRefCount(pScriptRes);
        }
    }

    if (rc != TCL_OK) {
        SEE_error_throw_sys(pInterp, 
            pInterp->RangeError, "%s", Tcl_GetStringResult(pTclInterp)
        );
    }
}

static void 
SeeTcl_Put(pInterp, pObj, pProp, pVal, flags)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
    struct SEE_value *pVal;
    int flags;
{
    SeeTclObject *pObject = (SeeTclObject *)pObj;
    SeeInterp *p = pObject->pTclSeeInterp;
    Tcl_Interp *pTclInterp = p->pTclInterp;
    Tcl_Obj *pScript = Tcl_DuplicateObj(pObject->pObj);
    int rc;
    int nObj = 0;

    Tcl_ListObjAppendElement(pTclInterp, pScript, Tcl_NewStringObj("Put", 3));
    Tcl_ListObjAppendElement(pTclInterp, pScript, stringToObj(pProp));
    Tcl_ListObjAppendElement(pTclInterp, pScript, argValueToTcl(p,pVal,&nObj));

    rc = Tcl_EvalObjEx(pTclInterp, pScript, TCL_GLOBAL_ONLY);
    removeTransientRefs(p, nObj);

    /* If the result of the [$obj Put] script was the literal string 
     * "native", then store this property in the pObject->native hash 
     * table.
     */
    if (0 == strcmp(Tcl_GetStringResult(pTclInterp), "native")) {
        struct SEE_object *pNative = (struct SEE_object *)(&pObject->native);
        flags |= SEE_ATTR_INTERNAL;
        SEE_native_put(&p->interp, pNative, pProp, pVal, flags);
    }

    if (rc != TCL_OK) {
        SEE_error_throw_sys(pInterp, 
            pInterp->TypeError, "%s", Tcl_GetStringResult(pTclInterp)
        );
    }
}

static int 
SeeTcl_CanPut(pInterp, pObj, pProp)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
{
    SeeTclObject *pObject = (SeeTclObject *)pObj;
    SeeInterp *pTclSeeInterp = pObject->pTclSeeInterp;
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    Tcl_Obj *pScript = Tcl_DuplicateObj(pObject->pObj);
    int rc;
    int ret;

    Tcl_ListObjAppendElement(pTclInterp, pScript, Tcl_NewStringObj("CanPut",6));
    Tcl_ListObjAppendElement(pTclInterp, pScript, 
            Tcl_NewUnicodeObj(pProp->data, pProp->length)
    );

    rc = Tcl_EvalObjEx(pTclInterp, pScript, TCL_GLOBAL_ONLY);
    if (
        rc != TCL_OK || TCL_OK != 
        Tcl_GetBooleanFromObj(pTclInterp, Tcl_GetObjResult(pTclInterp), &ret)
    ) {
        SEE_error_throw_sys(pInterp, 
            pInterp->RangeError, "%s", Tcl_GetStringResult(pTclInterp)
        );
    }
    return ret;
}
static int 
SeeTcl_HasProperty(pInterp, pObj, pProp)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
{
    SeeTclObject *pObject = (SeeTclObject *)pObj;
    SeeInterp *pTclSeeInterp = pObject->pTclSeeInterp;
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    Tcl_Obj *pScript = Tcl_DuplicateObj(pObject->pObj);
    int rc;
    int ret = 0;

    Tcl_ListObjAppendElement(pTclInterp, pScript, 
            Tcl_NewStringObj("HasProperty", 11)
    );
    Tcl_ListObjAppendElement(pTclInterp, pScript, 
            Tcl_NewUnicodeObj(pProp->data, pProp->length)
    );

    rc = Tcl_EvalObjEx(pTclInterp, pScript, TCL_GLOBAL_ONLY);
    if (
        rc != TCL_OK || TCL_OK != 
        Tcl_GetBooleanFromObj(pTclInterp, Tcl_GetObjResult(pTclInterp), &ret)
    ) {
        SEE_error_throw_sys(pInterp, 
            pInterp->RangeError, "%s", Tcl_GetStringResult(pTclInterp)
        );
    }
    return ret;
}
static int 
SeeTcl_Delete(pInterp, pObj, pProp)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
{
    SeeTclObject *pObject = (SeeTclObject *)pObj;
    SeeInterp *pTclSeeInterp = pObject->pTclSeeInterp;
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    Tcl_Obj *pScript = Tcl_DuplicateObj(pObject->pObj);
    int rc;

    Tcl_ListObjAppendElement(pTclInterp, pScript, Tcl_NewStringObj("Delete",6));
    Tcl_ListObjAppendElement(pTclInterp, pScript, 
            Tcl_NewUnicodeObj(pProp->data, pProp->length)
    );

    rc = Tcl_EvalObjEx(pTclInterp, pScript, TCL_GLOBAL_ONLY);
    if (rc != TCL_OK) {
        SEE_error_throw_sys(pInterp, 
            pInterp->RangeError, "%s", Tcl_GetStringResult(pTclInterp)
        );
    }

    return 0;
}

static void 
SeeTcl_DefaultValue(pInterp, pObj, pHint, pRes)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_value *pHint;
    struct SEE_value *pRes;
{
    SeeTclObject *pObject = (SeeTclObject *)pObj;
    SeeInterp *pTclSeeInterp = pObject->pTclSeeInterp;
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    Tcl_Obj *pScript = Tcl_DuplicateObj(pObject->pObj);
    int rc;

    Tcl_ListObjAppendElement(
        pTclInterp, pScript, Tcl_NewStringObj("DefaultValue", 12)
    );
    rc = Tcl_EvalObjEx(pTclInterp, pScript, TCL_GLOBAL_ONLY);

    if (rc == TCL_OK) {
        objToValue(pTclSeeInterp, Tcl_GetObjResult(pTclInterp), pRes);
    } else {
        struct SEE_string *pString;
        pString = SEE_string_sprintf(
            &pTclSeeInterp->interp, "%s", Tcl_GetString(pObject->pObj)
        );
        SEE_SET_STRING(pRes, pString);
    }
}
static struct SEE_enum * 
tclEnumerator(struct SEE_interpreter *, struct SEE_object *);
static struct SEE_enum *
SeeTcl_Enumerator(pInterp, pObj)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
{
    return tclEnumerator(pInterp, pObj);
}
static void 
tclCallOrConstruct(pMethod, pInterp, pObj, pThis, argc, argv, pRes)
    Tcl_Obj *pMethod;
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_object *pThis;
    int argc;
    struct SEE_value **argv;
    struct SEE_string *pRes;
{
    SeeTclObject *pObject = (SeeTclObject *)pObj;
    SeeInterp *pTclSeeInterp = pObject->pTclSeeInterp;
    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    Tcl_Obj *pScript = Tcl_DuplicateObj(pObject->pObj);
    int rc;
    int ii;
    int nObj = 0;

    Tcl_ListObjAppendElement(0, pScript, pMethod);
    /* TODO: The "this" object */
    Tcl_ListObjAppendElement(0, pScript, Tcl_NewStringObj("THIS", 4));
    for (ii = 0; ii < argc; ii++) {
        Tcl_ListObjAppendElement(0, pScript, 
            argValueToTcl(pTclSeeInterp, argv[ii], &nObj)
        );
    }

    rc = Tcl_EvalObjEx(pTclInterp, pScript, TCL_GLOBAL_ONLY);
    removeTransientRefs(pTclSeeInterp, nObj);
    if (rc != TCL_OK) {
        SEE_error_throw_sys(pInterp, 
            pInterp->RangeError, "%s", Tcl_GetStringResult(pTclInterp)
        );
    }

    rc = objToValue(pTclSeeInterp, Tcl_GetObjResult(pTclInterp), pRes);
    if (rc != TCL_OK) {
        SEE_error_throw_sys(pInterp, 
            pInterp->RangeError, "%s", Tcl_GetStringResult(pTclInterp)
        );
    }
}
static void 
SeeTcl_Construct(pInterp, pObj, pThis, argc, argv, pRes)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_object *pThis;
    int argc;
    struct SEE_value **argv;
    struct SEE_string *pRes;
{
    tclCallOrConstruct(
        Tcl_NewStringObj("Construct", 9),
        pInterp, pObj, pThis, argc, argv, pRes
    );
}
static void 
SeeTcl_Call(pInterp, pObj, pThis, argc, argv, pRes)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_object *pThis;
    int argc;
    struct SEE_value **argv;
    struct SEE_string *pRes;
{
    tclCallOrConstruct(
        Tcl_NewStringObj("Call", 4),
        pInterp, pObj, pThis, argc, argv, pRes
    );
}
static int 
SeeTcl_HasInstance(pInterp, pObj, pInstance)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_value *pInstance;
{
    printf("HasInstance!!!\n");
    return 0;
}
static void *
SeeTcl_GetSecDomain(pInterp, pObj)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
{
    printf("GetSecDomain!!!\n");
    return 0;
}

static struct SEE_objectclass SeeTclObjectVtbl = {
    "Object",
    SeeTcl_Get,
    SeeTcl_Put,
    SeeTcl_CanPut,
    SeeTcl_HasProperty,
    SeeTcl_Delete,
    SeeTcl_DefaultValue,
    SeeTcl_Enumerator,
    SeeTcl_Construct,
    SeeTcl_Call,
    SeeTcl_HasInstance,
    SeeTcl_GetSecDomain
};
static struct SEE_objectclass *getVtbl() {
    return &SeeTclObjectVtbl;
}


/* Sub-class of SEE_enum (v-table SeeTclEnumVtbl, see below) for iterating
 * through the properties of a Tcl-based object.
 */
typedef struct SeeTclEnum SeeTclEnum;
struct SeeTclEnum {
  struct SEE_enum base;

  /* Variables for iterating through Tcl properties */
  int iCurrent;
  int nString;
  struct SEE_string **aString;

  /* Enumerator for iterating through native properties */
  struct SEE_enum *pNativeEnum;
};

static struct SEE_string *
SeeTclEnum_Next(pSeeInterp, pEnum, pFlags)
    struct SEE_interpreter *pSeeInterp;
    struct SEE_enum *pEnum;
    int *pFlags;                          /* OUT: true for "do not enumerate" */
{
    SeeTclEnum *pSeeTclEnum = (SeeTclEnum *)pEnum;
    if (pSeeTclEnum->iCurrent < pSeeTclEnum->nString) {
        if (pFlags) *pFlags = 0;
        return pSeeTclEnum->aString[pSeeTclEnum->iCurrent++];
    }
    return SEE_ENUM_NEXT(pSeeInterp, pSeeTclEnum->pNativeEnum, pFlags);
}

static struct SEE_enumclass SeeTclEnumVtbl = {
  0,  /* Unused */
  SeeTclEnum_Next
};


static struct SEE_enum *
tclEnumerator(pInterp, pObj)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
{
    SeeTclObject *pObject = (SeeTclObject *)pObj;
    SeeInterp *pTclSeeInterp = pObject->pTclSeeInterp;

    Tcl_Interp *pTclInterp = pTclSeeInterp->pTclInterp;
    Tcl_Obj *pScript = Tcl_DuplicateObj(pObject->pObj);

    Tcl_Obj *pRet = 0;       /* Return value of script */
    Tcl_Obj **apRet = 0;     /* List elements of pRet */
    int nRet = 0;            /* size of apString */

    SeeTclEnum *pEnum;
    int ii;

    Tcl_ListObjAppendElement(0, pScript, Tcl_NewStringObj("Enumerator", 10));
    if (
        TCL_OK != Tcl_EvalObjEx(pTclInterp, pScript, TCL_GLOBAL_ONLY) ||
        0      == (pRet = Tcl_GetObjResult(pTclInterp)) ||
        TCL_OK != Tcl_ListObjGetElements(pTclInterp, pRet, &nRet, &apRet)
    ) {
        SEE_error_throw_sys(pInterp, 
            pInterp->RangeError, "%s", Tcl_GetStringResult(pTclInterp)
        );
    }

    pEnum = SEE_malloc(&pTclSeeInterp->interp,
        sizeof(SeeTclEnum) + sizeof(struct SEE_String *) * nRet
    );
    pEnum->base.enumclass = &SeeTclEnumVtbl;
    pEnum->iCurrent = 0;
    pEnum->nString = nRet;
    pEnum->aString = (struct SEE_string **)(&pEnum[1]);
    
    for (ii = 0; ii < nRet; ii++) {
        pEnum->aString[ii] = SEE_string_sprintf(
             &pTclSeeInterp->interp, "%s", Tcl_GetString(apRet[ii])
        );
    }

    pEnum->pNativeEnum = SEE_native_enumerator(
        &pTclSeeInterp->interp, (struct SEE_object *)&pObject->native
    );

    return (struct SEE_enum *)pEnum;
}

typedef struct Hv3GlobalObject Hv3GlobalObject;
struct Hv3GlobalObject {
    struct SEE_object object;
    struct SEE_object *pWindow;
    struct SEE_object *pGlobal;
};

static struct SEE_object *
hv3GlobalPick(pInterp, pObj, pProp)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
{
    Hv3GlobalObject *p = (Hv3GlobalObject *)pObj;
    if (SEE_OBJECT_HASPROPERTY(pInterp, p->pWindow, pProp)) {
        return p->pWindow;
    } 
    return p->pGlobal;
}

static void 
Hv3Global_Get(pInterp, pObj, pProp, pRes)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
    struct SEE_value *pRes;
{
    SEE_OBJECT_GET(pInterp, hv3GlobalPick(pInterp, pObj, pProp), pProp, pRes);
}
static void 
Hv3Global_Put(pInterp, pObj, pProp, pVal, flags)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
    struct SEE_value *pVal;
    int flags;
{
    struct SEE_object *p = hv3GlobalPick(pInterp, pObj, pProp);
    SEE_OBJECT_PUT(pInterp, p, pProp, pVal, flags);
}
static int 
Hv3Global_CanPut(pInterp, pObj, pProp)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
{
    struct SEE_object *p = hv3GlobalPick(pInterp, pObj, pProp);
    return SEE_OBJECT_CANPUT(pInterp, p, pProp);
}
static int 
Hv3Global_HasProperty(pInterp, pObj, pProp)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
{
    Hv3GlobalObject *p = (Hv3GlobalObject *)pObj;
    return (
        SEE_OBJECT_HASPROPERTY(pInterp, p->pWindow, pProp) ||
        SEE_OBJECT_HASPROPERTY(pInterp, p->pGlobal, pProp)
    );
}
static int 
Hv3Global_Delete(pInterp, pObj, pProp)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_string *pProp;
{
    Hv3GlobalObject *p = (Hv3GlobalObject *)pObj;
    return SEE_OBJECT_DELETE(pInterp, p->pGlobal, pProp);
}
static void 
Hv3Global_DefaultValue(pInterp, pObj, pHint, pRes)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
    struct SEE_value *pHint;
    struct SEE_value *pRes;
{
    Hv3GlobalObject *p = (Hv3GlobalObject *)pObj;
    SEE_OBJECT_DEFAULTVALUE(pInterp, p->pGlobal, pHint, pRes);
}

static struct SEE_enum * 
Hv3Global_Enumerator(struct SEE_interpreter *, struct SEE_object *);

static struct SEE_objectclass Hv3GlobalObjectVtbl = {
    "Hv3GlobalObject",
    Hv3Global_Get,
    Hv3Global_Put,
    Hv3Global_CanPut,
    Hv3Global_HasProperty,
    Hv3Global_Delete,
    Hv3Global_DefaultValue,
    Hv3Global_Enumerator,
    0,
    0,
    0,
    0
};

typedef struct Hv3GlobalEnum Hv3GlobalEnum;
struct Hv3GlobalEnum {
    struct SEE_enum base;
    struct SEE_enum *pWindowEnum;
    struct SEE_enum *pGlobalEnum;
};

static struct SEE_string *
Hv3GlobalEnum_Next(pSeeInterp, pEnum, pFlags)
    struct SEE_interpreter *pSeeInterp;
    struct SEE_enum *pEnum;
    int *pFlags;                          /* OUT: true for "do not enumerate" */
{
    Hv3GlobalEnum *p = (Hv3GlobalEnum *)pEnum;
    struct SEE_string *pRet;

    pRet = SEE_ENUM_NEXT(pSeeInterp, p->pWindowEnum, pFlags);
    if (!pRet) {
        pRet = SEE_ENUM_NEXT(pSeeInterp, p->pGlobalEnum, pFlags);
    }

    return pRet;
}

static struct SEE_enumclass Hv3GlobalEnumVtbl = {
  0,  /* Unused */
  Hv3GlobalEnum_Next
};

static struct SEE_enum *
Hv3Global_Enumerator(pInterp, pObj)
    struct SEE_interpreter *pInterp;
    struct SEE_object *pObj;
{
    Hv3GlobalObject *p = (Hv3GlobalObject *)pObj;
    Hv3GlobalEnum *pEnum;

    pEnum = SEE_NEW(pInterp, Hv3GlobalEnum);
    pEnum->base.enumclass = &Hv3GlobalEnumVtbl;
    pEnum->pWindowEnum = SEE_OBJECT_ENUMERATOR(pInterp, p->pWindow);
    pEnum->pGlobalEnum = SEE_OBJECT_ENUMERATOR(pInterp, p->pGlobal);
    return ((struct SEE_enum *)pEnum);
}

struct SEE_scope;
struct SEE_scope {
  struct SEE_scope *next;
  struct SEE_object *obj;
};

/*
 *---------------------------------------------------------------------------
 *
 * installHv3Global --
 *
 *     Install an object (argument pWindow) as the hv3-global object
 *     for interpreter pTclSeeInterp. This function should only be
 *     called once in the lifetime of pTclSeeInterp.
 *
 *     The supplied object, pWindow, is attached to the real global 
 *     object (SEE_interp.Global) so that the following SEE_objectclass
 *     interface functions are intercepted and handled as follows:
 *
 *         Get:
 *             If the HasProperty() method of pWindow returns true,
 *             call the Get method of pWindow. Otherwise, call Get on
 *             the real global object.
 *         Put:
 *             If the HasProperty() method of pWindow returns true,
 *             call the Put method of pWindow. Otherwise, call Put on
 *             the real global object.
 *         CanPut:
 *             If the HasProperty() method of pWindow returns true,
 *             call the CanPut method of pWindow. Otherwise, call CanPut 
 *             on the real global object.
 *         HasProperty():
 *             Return the logical OR of HasProperty called on pWindow and
 *             the real global object.
 *         Enumerator():
 *             Return a wrapper SEE_enum object that first iterates through
 *             the entries returned by a pWindow iterator, then through
 *             the entries returned by the real global object.
 *
 *     The Delete() and DefaultValue() methods are passed through to
 *     the real global object.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *
 *---------------------------------------------------------------------------
 */
static void 
installHv3Global(pTclSeeInterp, pWindow)
    SeeInterp *pTclSeeInterp;
    struct SEE_object *pWindow;
{
    struct SEE_scope *pScope;
    struct SEE_interpreter *p = &pTclSeeInterp->interp;
    Hv3GlobalObject *pGlobal = SEE_NEW(p, Hv3GlobalObject);

    assert(p->Global->objectclass != &Hv3GlobalObjectVtbl);

    pGlobal->object.objectclass = &Hv3GlobalObjectVtbl;
    pGlobal->object.Prototype = p->Global->Prototype;
    pGlobal->pWindow = pWindow;
    pGlobal->pGlobal = p->Global;

    for (pScope = p->Global_scope; pScope; pScope = pScope->next) {
        if (pScope->obj == p->Global) {
            pScope->obj = (struct SEE_object *)pGlobal;
        }
    }
    p->Global = (struct SEE_object *)pGlobal;
}

#define JSTOKEN_OPEN_BRACKET    1
#define JSTOKEN_CLOSE_BRACKET   2
#define JSTOKEN_OPEN_BRACE      3
#define JSTOKEN_CLOSE_BRACE     4
#define JSTOKEN_SEMICOLON       5
#define JSTOKEN_NEWLINE         6
#define JSTOKEN_SPACE           7

#define JSTOKEN_WORD            8
#define JSTOKEN_PUNC            9

static int
jsToken(zCode, ePrevToken, peToken)
    const char *zCode;     /* String to read token from */
    int ePrevToken;                 /* Previous token type */
    int *peToken;                   /* OUT: Token type */
{
    int nToken = 1;
    int eToken = 0;

    assert(*zCode);

    unsigned char aIsPunct[128] = {
        0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0, 0, 0, 0, 0,     /* 0x00 - 0x0F */
        0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0, 0, 0, 0, 0,     /* 0x10 - 0x1F */
        1, 1, 1, 1, 1, 1, 1, 1,    1, 1, 1, 1, 1, 1, 1, 1,     /* 0x20 - 0x2F */
        0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 1, 1, 1, 1, 1, 1,     /* 0x30 - 0x3F */
        1, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0, 0, 0, 0, 0,     /* 0x40 - 0x4F */
        0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 1, 0, 1, 1, 0,     /* 0x50 - 0x5F */
        1, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0, 0, 0, 0, 0,     /* 0x60 - 0x6F */
        0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 1, 1, 1, 1, 0,     /* 0x70 - 0x7F */
    };
    
    switch (*zCode) {
        case '(':  eToken = JSTOKEN_OPEN_BRACKET; break;
        case ')':  eToken = JSTOKEN_CLOSE_BRACKET; break;
        case '{':  eToken = JSTOKEN_OPEN_BRACE; break;
        case '}':  eToken = JSTOKEN_CLOSE_BRACE; break;
        case ';':  eToken = JSTOKEN_SEMICOLON; break;
        case '\n': eToken = JSTOKEN_NEWLINE; break;
        case ' ':  eToken = JSTOKEN_SPACE; break;

        case '/': {
            /* C++ comment */
            if (zCode[1] == '/') {
              eToken = JSTOKEN_WORD;
              while (zCode[nToken] && zCode[nToken] != '\n') nToken++;
              if (zCode[nToken]) nToken++;
              break;
            }

            /* C comment */
            if (zCode[1] == '*') {
              eToken = JSTOKEN_WORD;
              while (zCode[nToken]) {
                if (zCode[nToken] == '/' && zCode[nToken - 1] == '*') {
                    nToken++;
                    break;
                }
                nToken++;
              }
              break;
            }

            /* Division sign */
            if (
                ePrevToken == JSTOKEN_WORD || 
                ePrevToken == JSTOKEN_CLOSE_BRACKET
            ) {
                eToken = JSTOKEN_PUNC;
                break;
            }

            /* Regex literal (fall through) */
        }

        case '"':
        case '\'': {
          int ii;
          for (ii = 1; zCode[ii] && zCode[ii] != zCode[0]; ii++) {
            if (zCode[ii] == '\\' && zCode[ii + 1]) ii++;
          }
          eToken = JSTOKEN_WORD;
          nToken = ii + (zCode[ii] ? 1 : 0);
          break;
        }

        default: {
            char c = *zCode;
            if (c >= 0 && aIsPunct[(int)c]) {
                eToken = JSTOKEN_PUNC;
            } else {
                int ii = 1;
                for ( ; zCode[ii] > 0 && 0 == aIsPunct[(int)zCode[ii]]; ii++);
                eToken = JSTOKEN_WORD;
                nToken = ii;
            }
            break;
        }
    }

    assert(eToken != 0);
    *peToken = eToken;
    return nToken;
}

/*
 *---------------------------------------------------------------------------
 *
 * tclSeeFormat --
 *
 *         ::see::format JAVASCRIPT-CODE
 *
 * Results:
 *     Standard Tcl result.
 *
 * Side effects:
 *     None
 *
 *---------------------------------------------------------------------------
 */
static int 
tclSeeFormat(clientData, interp, objc, objv)
    ClientData clientData;             /* The HTML widget data structure */
    Tcl_Interp *interp;                /* Current interpreter. */
    int objc;                          /* Number of arguments. */
    Tcl_Obj *CONST objv[];             /* Argument strings. */
{
    char *zCode;
    char *zEnd;
    int nCode;

    int eToken = JSTOKEN_SEMICOLON;
    Tcl_Obj *pRet;

    static const int INDENT_SIZE = 2;
    const int MAX_INDENT = 40;
    static const char zWhite[] = "                                        ";

    #define IGNORE_NONE 0
    #define IGNORE_SPACE 1
    #define IGNORE_NEWLINE 2
    int eIgnore = IGNORE_NONE;
    int iIndent = 0;
    int iBracket = 0;

    assert(strlen(zWhite) == MAX_INDENT);

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "JAVASCRIPT-CODE");
        return TCL_ERROR;
    }
    pRet = Tcl_NewObj();
    Tcl_IncrRefCount(pRet);

    zCode = Tcl_GetStringFromObj(objv[1], &nCode);
    zEnd = &zCode[nCode];
    while (zCode < zEnd) {
        int nToken;
 
        /* Read a token from the input */
        char *zToken = zCode;
        nToken = jsToken(zCode, eToken, &eToken);
        zCode += nToken;

        switch (eToken) {
            case JSTOKEN_OPEN_BRACKET:  iBracket++; break;
            case JSTOKEN_CLOSE_BRACKET: iBracket--; break;

            case JSTOKEN_OPEN_BRACE:
                if (iBracket == 0) {
                  iIndent += INDENT_SIZE;
                  Tcl_AppendToObj(pRet, "{\n", 2);
                  Tcl_AppendToObj(pRet, zWhite, MIN(MAX_INDENT, iIndent));
                  eIgnore = IGNORE_NEWLINE;
                  zToken = 0;
                }
                break;

            case JSTOKEN_CLOSE_BRACE:
                if (iBracket == 0) {
                  iIndent -= INDENT_SIZE;
                  if (eIgnore == IGNORE_NONE) {
                      Tcl_AppendToObj(pRet, "\n", 1);
                      Tcl_AppendToObj(pRet, zWhite, MIN(MAX_INDENT, iIndent));
                  } 
                  Tcl_AppendToObj(pRet, "}\n", 2);
                  Tcl_AppendToObj(pRet, zWhite, MIN(MAX_INDENT, iIndent));
                  eIgnore = IGNORE_NEWLINE;
                  zToken = 0;
                }
                break;

            case JSTOKEN_SEMICOLON: 
                if (iBracket == 0) {
                  Tcl_AppendToObj(pRet, ";\n", 2);
                  Tcl_AppendToObj(pRet, zWhite, MIN(MAX_INDENT, iIndent));
                  eIgnore = IGNORE_NEWLINE;
                  zToken = 0;
                }
                break;

            case JSTOKEN_NEWLINE: 
                if (eIgnore == IGNORE_NEWLINE) {
                    eIgnore = IGNORE_SPACE;
                    zToken = 0;
                }
                break;

            case JSTOKEN_SPACE: 
                if (eIgnore != IGNORE_NONE) {
                    zToken = 0;
                }
                break;

            case JSTOKEN_WORD:
            case JSTOKEN_PUNC:
                eIgnore = IGNORE_NONE;
                break;

            default:
                assert(!"Bad token type");
        }

        if (zToken) {
            Tcl_AppendToObj(pRet, zToken, nToken);
        }
    }

    Tcl_SetObjResult(interp, pRet);
    Tcl_DecrRefCount(pRet);
    return TCL_OK;
}

int 
Tclsee_Init(interp)
    Tcl_Interp *interp;
{
    /* Require stubs libraries version 8.4 or greater. */
#ifdef USE_TCL_STUBS
    if (Tcl_InitStubs(interp, "8.4", 0) == 0) {
        return TCL_ERROR;
    }
#endif

    Tcl_PkgProvide(interp, "Tclsee", "0.1");
    Tcl_CreateObjCommand(interp, "::see::interp", tclSeeInterp, 0, 0);
    Tcl_CreateObjCommand(interp, "::see::format", tclSeeFormat, 0, 0);
    return TCL_OK;
}

/*----------------------------------------------------------------------- 
 * Event target container object notes:
 *
 *     set container [$interp eventtarget]
 *
 *     $container addEventListener    TYPE LISTENER USE-CAPTURE
 *     $container removeEventListener TYPE LISTENER USE-CAPTURE
 *     $container setLegacyListener   TYPE LISTENER
 *     $container runEvent            TYPE USE-CAPTURE THIS JS-ARG
 *     $container destroy
 *
 * where:
 * 
 *     TYPE is an event type - e.g. "click", "load" etc. Case insensitive.
 *     LISTENER is a javascript object reference (to a callable object)
 *     USE-CAPTURE is a boolean.
 *     JS-ARG is a typed javascript value to pass to the event handlers
 *
 * Implementing the EventTarget interface (see DOM Level 2) is difficult
 * using the current ::see::interp interface. The following code implements
 * a data structure to help with this while storing javascript function
 * references in garbage collected memory (so that they are not collected
 * prematurely).
 *
 * This data structure allows for storage of zero or more more pointers
 * to javascript objects. For each event "type" either zero or one 
 * legacy event-listener and any number of normal event-listeners may
 * be stored. An event type is any string.
 *
 * Calling [destroy] exactly once for each call to [eventtarget] is 
 * mandatory. Otherwise -> memory leak. Also, all event-target containers
 * should be destroyed before the interpreter that created them. TODO:
 * there should be an assert() to check this when NDEBUG is not defined.
 *
 * Implementation is in two C-functions below:
 *
 *     eventTargetNew()
 *     eventTargetMethod()
 */
typedef struct EventTarget EventTarget;
typedef struct ListenerContainer ListenerContainer;
typedef struct EventType EventType;

static int iNextEventTarget = 0;

struct EventTarget {
  SeeInterp *pTclSeeInterp;
  EventType *pTypeList;
};

struct EventType {
  char *zType;
  ListenerContainer *pListenerList;
  struct SEE_object *pLegacyListener;
  EventType *pNext;
};

struct ListenerContainer {
  int isCapture;                  /* True if a capturing event */
  struct SEE_object *pListener;   /* Listener function */
  ListenerContainer *pNext;       /* Next listener on this event type */
};

static struct SEE_object *
eventTargetValue(pTclSeeInterp, pEventTarget, pEvent)
    SeeInterp *pTclSeeInterp;
    Tcl_Obj *pEventTarget;
    Tcl_Obj *pEvent;
{
    Tcl_Interp *interp = pTclSeeInterp->pTclInterp;
    Tcl_CmdInfo info;
    int rc;
    EventTarget *p;
    EventType *pType;

    rc = Tcl_GetCommandInfo(interp, Tcl_GetString(pEventTarget), &info);
    if (rc != TCL_OK) return 0;
    p = (EventTarget *)info.clientData;

    for (
        pType = p->pTypeList;
        pType && strcasecmp(Tcl_GetString(pEvent), pType->zType);
        pType = pType->pNext
    );

    return (pType ? pType->pLegacyListener : 0);
}

/*
 *---------------------------------------------------------------------------
 *
 * eventTargetDelete --
 *
 *     Called to delete an EventTarget object.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Frees the EventTarget object passed as an argument.
 *
 *---------------------------------------------------------------------------
 */
static void 
eventTargetDelete(clientData)
    ClientData clientData;          /* Pointer to the EventTarget structure */
{
    EventTarget *p = (EventTarget *)clientData;
    GC_FREE(p);
}

static Tcl_Obj *
listenerToString(pSeeInterp, pListener)
    struct SEE_interpreter *pSeeInterp;
    struct SEE_object *pListener;
{
    struct SEE_value val;
    struct SEE_value res;

    SEE_OBJECT_DEFAULTVALUE(pSeeInterp, pListener, 0, &val);
    SEE_ToString(pSeeInterp, &val, &res);
    return Tcl_NewUnicodeObj(
        res.u.string->data, res.u.string->length
    );
}

/*
 *---------------------------------------------------------------------------
 *
 * eventTargetDump --
 *
 *     This function is used to introspect the event-target object from
 *     the Tcl level. The return value is a list. Each element of
 *     the list takes the following form:
 *
 *       {EVENT-TYPE LISTENER-TYPE JAVASCRIPT}
 *
 *     where EVENT-TYPE is the event-type string passed to [addEventListener]
 *     or [setLegacyListener]. LISTENER-TYPE is one of "legacy", "capturing"
 *     or "non-capturing". JAVASCRIPT is the "tostring" version of the
 *     js object to call to process the event.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static void 
eventTargetDump(interp, p)
    Tcl_Interp *interp;
    EventTarget *p;
{
    EventType *pType;
    Tcl_Obj *apRow[3];
    Tcl_Obj *pRet;
    struct SEE_interpreter *pSeeInterp = &(p->pTclSeeInterp->interp);

    pRet = Tcl_NewObj();
    Tcl_IncrRefCount(pRet);

    for (pType = p->pTypeList; pType; pType = pType->pNext) {
        ListenerContainer *pL;

        Tcl_Obj *pEventType = Tcl_NewStringObj(pType->zType, -1);
        Tcl_IncrRefCount(pEventType);
        apRow[0] = pEventType;

        if (pType->pLegacyListener) {
            apRow[1] = Tcl_NewStringObj("legacy", -1);
            apRow[2] = listenerToString(pSeeInterp, pType->pLegacyListener);
            Tcl_ListObjAppendElement(interp, pRet, Tcl_NewListObj(3, apRow));
        }

        for (pL = pType->pListenerList; pL; pL = pL->pNext) {
            const char *zType = (pL->isCapture?"capturing":"non-capturing");
            apRow[1] = Tcl_NewStringObj(zType, -1);
            apRow[2] = listenerToString(pSeeInterp, pL->pListener);
            Tcl_ListObjAppendElement(interp, pRet, Tcl_NewListObj(3, apRow));
        }

        Tcl_DecrRefCount(pEventType);
    }
    

    Tcl_SetObjResult(interp, pRet);
    Tcl_DecrRefCount(pRet);
}

/*
 *---------------------------------------------------------------------------
 *
 * eventTargetMethod --
 *
 *     $eventtarget addEventListener    TYPE LISTENER USE-CAPTURE
 *
 *     $eventtarget removeEventListener TYPE LISTENER USE-CAPTURE
 *
 *     $eventtarget setLegacyListener   TYPE LISTENER
 *
 *     $eventtarget removeLegacyListener TYPE
 *
 *     $eventtarget runEvent            TYPE USE-CAPTURE THIS JS-ARG
 *
 *     $eventtarget destroy
 *         Destroy the event-target object. This is eqivalent to
 *         evaluating [rename $eventtarget ""].
 *
 * Results:
 *     TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *     Whatever the method does (see above).
 *
 *---------------------------------------------------------------------------
 */
static int 
eventTargetMethod(clientData, interp, objc, objv)
    ClientData clientData;          /* Pointer to the EventTarget structure */
    Tcl_Interp *interp;             /* Current Tcl interpreter. */
    int objc;                       /* Number of arguments. */
    Tcl_Obj *CONST objv[];          /* Argument strings. */
{
    EventTarget *p = (EventTarget *)clientData;
    struct SEE_interpreter *pSeeInterp = &(p->pTclSeeInterp->interp);

    EventType *pType = 0;
    struct SEE_object *pListener = 0;
    int iChoice;

    enum EVENTTARGET_enum {
        ET_ADD,
        ET_DESTROY,
        ET_DUMP,
        ET_REMOVE,
        ET_REMLEGACY,
        ET_RUNEVENT,
        ET_LEGACY,
        ET_INLINE
    };
    enum EVENTTARGET_enum eChoice;

    static const struct EventTargetSubCommand {
        const char *zCommand;
        enum EVENTTARGET_enum eSymbol;
        int nArgs;
        char *zArgs;
    } aSubCommand[] = {
        {"addEventListener",     ET_ADD,       3, "TYPE LISTENER USE-CAPTURE"},
        {"destroy",              ET_DESTROY,   0, ""},
        {"dump",                 ET_DUMP,      0, ""},
        {"removeEventListener",  ET_REMOVE,    3, "TYPE LISTENER USE-CAPTURE"},
        {"removeLegacyListener", ET_REMLEGACY, 1, "TYPE"},
        {"runEvent",             ET_RUNEVENT,  4, "TYPE CAPTURE THIS JS-ARG"},
        {"setLegacyListener",    ET_LEGACY,    2, "TYPE LISTENER"},
        {"setLegacyScript",      ET_INLINE,    2, "TYPE JAVASCRIPT"},
        {0, 0, 0, 0}
    };

    int rc = TCL_OK;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "SUBCOMMAND ...");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[1], aSubCommand, 
            sizeof(struct EventTargetSubCommand), "option", 0, &iChoice) 
    ){
        return TCL_ERROR;
    }

    if (objc != aSubCommand[iChoice].nArgs + 2) {
        Tcl_WrongNumArgs(interp, 2, objv, aSubCommand[iChoice].zArgs);
        return TCL_ERROR;
    }

    /* If this is an ADD, LEGACY, INLINE, RUNEVENT or REMOVE operation, 
     * search for an EventType that matches objv[2]. If it is an ADD, 
     * LEGACY or INLINE, create the EventType if it does not already exist. 
     */
    eChoice = aSubCommand[iChoice].eSymbol;
    if (
        eChoice == ET_REMOVE || eChoice == ET_RUNEVENT || 
        eChoice == ET_ADD || eChoice == ET_LEGACY || eChoice == ET_INLINE ||
        eChoice == ET_REMLEGACY
    ) {
        for (
            pType = p->pTypeList;
            pType && strcasecmp(Tcl_GetString(objv[2]), pType->zType);
            pType = pType->pNext
        );
    }
    if (
        (!pType) && 
        (eChoice == ET_ADD || eChoice == ET_LEGACY || eChoice == ET_INLINE)
    ) {
        int nType;
        char *zType = Tcl_GetStringFromObj(objv[2], &nType);

        nType++;
        pType = (EventType *)SEE_malloc(pSeeInterp, sizeof(EventType) + nType);
        memset(pType, 0, sizeof(EventType));

        pType->zType = (char *)&pType[1];
        strcpy(pType->zType, zType);
        pType->pNext = p->pTypeList;
        p->pTypeList = pType;
    }

    /* If this is an ADD, LEGACY, REMOVE operation, convert objv[3]
     * (the LISTENER) into a SEE_object pointer.
     */
    if (eChoice == ET_ADD || eChoice == ET_LEGACY || eChoice == ET_REMOVE) {
        pListener = findOrCreateObject(p->pTclSeeInterp, objv[3]);
    }
    if (eChoice == ET_INLINE) {
        struct SEE_input *pInputCode;
        pInputCode = SEE_input_utf8(pSeeInterp, Tcl_GetString(objv[3]));
        pListener = SEE_Function_new(pSeeInterp, 0, 0, pInputCode);
        SEE_INPUT_CLOSE(pInputCode);
    }

    Tcl_ResetResult(interp);

    switch (eChoice) {
        case ET_ADD: {
            ListenerContainer *pListenerContainer;
            int isCapture;
            if (TCL_OK != Tcl_GetBooleanFromObj(interp, objv[4], &isCapture)) {
                return TCL_ERROR;
            }
            isCapture = (isCapture ? 1 : 0);

            pListenerContainer = SEE_NEW(pSeeInterp, ListenerContainer);
            pListenerContainer->isCapture = isCapture;
            pListenerContainer->pListener = pListener;
            pListenerContainer->pNext = pType->pListenerList;
            pType->pListenerList = pListenerContainer;

            break;
        }

        case ET_REMOVE: {
            ListenerContainer **ppListenerContainer;
            int isCapture;

            if (!pType) break;
            if (TCL_OK != Tcl_GetBooleanFromObj(interp, objv[4], &isCapture)) {
                return TCL_ERROR;
            }
            isCapture = (isCapture ? 1 : 0);

            ppListenerContainer = &pType->pListenerList;
            while (*ppListenerContainer) {
                ListenerContainer *pL = *ppListenerContainer;
                if (pL->isCapture==isCapture && pL->pListener==pListener) {
                    *ppListenerContainer = pL->pNext;
                } else {
                    ppListenerContainer = &pL->pNext;
                }
            }

            break;
        }

        case ET_INLINE:
        case ET_LEGACY:
        case ET_REMLEGACY:
            assert(pType || (!pListener && eChoice == ET_REMLEGACY));
            if (pType) {
                pType->pLegacyListener = pListener;
            }
            break;

        /*
         * $eventtarget runEvent TYPE IS-CAPTURE THIS EVENT
         * 
         *     The return value of this command is one of the following:
         *
         *         "prevent" - A legacy handler returned false, indicating
         *                     that the default browser action should
         *                     be cancelled.
         *
         *         "ok"      - Event handlers were run.
         *
         *         ""        - There were no event handlers to run.
         *
         *     If an error or unhandled exception occurs in the javascript,
         *     a Tcl exception is thrown.
         */
        case ET_RUNEVENT: {
            ListenerContainer *pL;

            /* The result of this Tcl command. eRes is set to an index
             * into azRes. 
             */
            char const *azRes[] = {"", "ok", "prevent"};
            int eRes = 0;

	    /* TODO: Try-catch context to execute the javascript callbacks in.
             * At the moment only a single try-catch is used for all
             * W3C and legacy listeners. So if one throws an exception
             * the rest will not run. This is probably wrong, but fixing
	     * it means figuring out some way to return the error information
	     * to the browser. i.e. the current algorithm is:
             *
             *     TRY {
             *         FOREACH (W3C listener) { ... }
             *         IF (legacy listener) { ... }
             *     } CATCH (...) { ... }
             *
             */
            SEE_try_context_t try_ctxt;

            struct SEE_object *pThis;

            /* The event object passed as an argument */
            struct SEE_object *pArg;
            struct SEE_value sArgValue;
            struct SEE_value *pArgV;

            int isCapture;
            if (TCL_OK != Tcl_GetBooleanFromObj(interp, objv[3], &isCapture)) {
                return TCL_ERROR;
            }
            isCapture = (isCapture ? 1 : 0);

            pThis = findOrCreateObject(p->pTclSeeInterp, objv[4]);
            pArg = findOrCreateObject(p->pTclSeeInterp, objv[5]);
            SEE_SET_OBJECT(&sArgValue, pArg);
            pArgV= &sArgValue;

            if (pType) {
                SEE_TRY (pSeeInterp, try_ctxt) {
                    struct SEE_object *pLegacy = pType->pLegacyListener;
    
                    for (pL = pType->pListenerList; pL; pL = pL->pNext) {
                        if (isCapture == pL->isCapture) {
                            struct SEE_value res;
                            struct SEE_object *p2 = pL->pListener;
                            SEE_OBJECT_CALL(
                                pSeeInterp, p2, pThis, 1, &pArgV, &res
                            );
                            eRes = MAX(eRes, 1);
                        }
                    }
        
                    if (!isCapture && pLegacy) {
                        struct SEE_value r;
                        SEE_OBJECT_CALL(
                            pSeeInterp, pLegacy, pThis, 1, &pArgV, &r
                        );
                        eRes = MAX(eRes, 1);
                        switch (SEE_VALUE_GET_TYPE(&r)) {
                            case SEE_BOOLEAN:
                                if (0 == r.u.boolean) eRes = MAX(eRes, 2);
                                break;
        
                            case SEE_NUMBER:
                                if (0 == ((int)r.u.number)) eRes = MAX(eRes, 2);
                                break;
        
                            default:
                                break;
                        }
                    }
                }
            }

            if (pType && SEE_CAUGHT(try_ctxt)) {
                rc = handleJavascriptError(p->pTclSeeInterp, &try_ctxt);
            } else {
                /* Note: The SEE_OBJECT_CALL() above may end up executing
                 * Tcl code in our main interpreter. Therefore it is important
                 * to set the command result here, after SEE_OBJECT_CALL().
                 *
                 * This was causing a bug earlier.
                 */
                Tcl_Obj *pRes = Tcl_NewStringObj(azRes[eRes], -1);
                Tcl_SetObjResult(interp, pRes);
            }
            break;
        }

        case ET_DESTROY:
            eventTargetDelete(clientData);
            break;

        case ET_DUMP:
            eventTargetDump(interp, p);
            break;

        default: assert(!"Can't happen");
    }

    return rc;
}

/*
 *---------------------------------------------------------------------------
 *
 * eventTargetNew --
 *
 *         $see_interp eventtarget
 *
 *     Create a new event-target container.
 *
 * Results:
 *     Standard Tcl result.
 *
 * Side effects:
 *     None
 *
 *---------------------------------------------------------------------------
 */
static int 
eventTargetNew(clientData, interp, objc, objv)
    ClientData clientData;             /* Pointer to the SeeInterp structure */
    Tcl_Interp *interp;                /* Current Tcl interpreter. */
    int objc;                          /* Number of arguments. */
    Tcl_Obj *CONST objv[];             /* Argument strings. */
{
    ClientData c;
    EventTarget *pNew;
    char zCmd[64];

    pNew = (EventTarget *)GC_MALLOC_UNCOLLECTABLE(sizeof(EventTarget));
    assert(pNew);
    memset(pNew, 0, sizeof(EventTarget));

    pNew->pTclSeeInterp = (SeeInterp *)clientData;

    sprintf(zCmd, "::see::et%d", iNextEventTarget++);
    c = (ClientData)pNew;
    Tcl_CreateObjCommand(interp, zCmd, eventTargetMethod, c, eventTargetDelete);
   
    Tcl_SetResult(interp, zCmd, TCL_VOLATILE);
    return TCL_OK;
}

