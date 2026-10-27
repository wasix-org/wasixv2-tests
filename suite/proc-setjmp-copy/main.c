// A jmp_buf is plain bytes, as on Linux: a copy restores the context it was set to, wherever it
// is kept, while the frame that called setjmp runs. bash saves its return_catch this way around
// a function body: copy the jmp_buf aside, setjmp it again in a nested frame, copy the old bytes
// back on return, and later longjmp to the outer context. The nested frame may have no shadow
// stack frame of its own. setjmp run again in a loop, or from sibling calls, keeps working.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: nested-local: val=1
//#ExpectedStdout: nested-global: val=2
//#ExpectedStdout: heap-copy: val=3
//#ExpectedStdout: siblings: rounds=10000 val=4
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf jb, stash;

// Saves jb in a local of its own frame, as bash's execute_function does.
__attribute__((noinline)) static void nested_local(void)
{
	jmp_buf saved;
	memcpy(saved, jb, sizeof jb);
	if (setjmp(jb) == 0) {
		/* nothing jumps here */
	}
	memcpy(jb, saved, sizeof jb);
}

// Keeps the copy in a global, so this frame needs no shadow stack.
__attribute__((noinline)) static void nested_global(void)
{
	memcpy(stash, jb, sizeof jb);
	setjmp(jb);
	memcpy(jb, stash, sizeof jb);
}

static volatile int rounds;

__attribute__((noinline)) static int sibling(void)
{
	if (setjmp(jb)) return 1;
	rounds++;
	return 0;
}

int main(void)
{
	int val = setjmp(jb);
	if (val == 0) {
		nested_local();
		longjmp(jb, 1);
	}
	printf("nested-local: val=%d\n", val);

	val = setjmp(jb);
	if (val == 0) {
		nested_global();
		longjmp(jb, 2);
	}
	printf("nested-global: val=%d\n", val);

	jmp_buf *copy = malloc(sizeof(jmp_buf));
	val = setjmp(jb);
	if (val == 0) {
		memcpy(*copy, jb, sizeof jb);
		memset(jb, 0, sizeof jb);
		longjmp(*copy, 3);
	}
	printf("heap-copy: val=%d\n", val);
	free(copy);

	for (int i = 0; i < 10000; i++) sibling();
	val = setjmp(jb);
	if (val == 0) longjmp(jb, 4);
	printf("siblings: rounds=%d val=%d\n", rounds, val);
	return 0;
}
