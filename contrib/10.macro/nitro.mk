CONTRIB_LIBS += $(wildcard contrib/10.macro/*.scm)

CONTRIB_TESTS += test-macro

test-macro: $(TEST_RUNNER)
	./$(TEST_RUNNER) contrib/10.macro/t/ir-macro.scm
