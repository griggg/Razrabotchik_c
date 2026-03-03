add_test([=[IntegratorTest.SimpleTest]=]  /home/egor/untitled5/test_integrator [==[--gtest_filter=IntegratorTest.SimpleTest]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[IntegratorTest.SimpleTest]=]  PROPERTIES DEF_SOURCE_LINE /home/egor/untitled5/tests/test_integrator.cpp:6 WORKING_DIRECTORY /home/egor/untitled5 SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==])
set(  test_integrator_TESTS IntegratorTest.SimpleTest)
