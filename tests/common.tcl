
package require Tkhtml
package require tcltest
tcltest::verbose {pass body error}

catch {rename finish_test ""}

if {[catch {incr ::nested_test_count}]} {set ::nested_test_count 1}

proc finish_test {} {
  catch {
    destroy .h
  }
  incr ::nested_test_count -1
  if {$::nested_test_count == 0}  {
    # Report an overall summary and exit non-zero if any test failed,
    # so that CI runners can detect failures from the process status.
    set total 0; set passed 0; set skipped 0; set failed 0
    catch {
      set total   $::tcltest::numTests(Total)
      set passed  $::tcltest::numTests(Passed)
      set skipped $::tcltest::numTests(Skipped)
      set failed  $::tcltest::numTests(Failed)
    }
    puts "finish_test: Total $total Passed $passed\
          Skipped $skipped Failed $failed"
    catch {
      destroy .
      catch {::tkhtml::htmlalloc}
    }
    exit [expr {$failed > 0 ? 1 : 0}]
  }
}


