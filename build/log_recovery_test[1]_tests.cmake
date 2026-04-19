add_test([=[RecoveryTest.AriesRedoAndUndoTest]=]  /home/rahul/aegisDB/build/log_recovery_test [==[--gtest_filter=RecoveryTest.AriesRedoAndUndoTest]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[RecoveryTest.AriesRedoAndUndoTest]=]  PROPERTIES WORKING_DIRECTORY /home/rahul/aegisDB/build SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==])
set(  log_recovery_test_TESTS RecoveryTest.AriesRedoAndUndoTest)
