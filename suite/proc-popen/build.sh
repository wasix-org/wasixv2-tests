# popen, pclose and system run their command with /bin/sh -c. The guest has no shell, so this
# fixture builds a minimal one (sh.c: `sh -c "prog args..."` splits on spaces and execs prog
# from /bin) next to `main`, and maps the build directory to /bin.
##MappedDirectory: .:/bin
##ExpectedStdout: read: hello from popen
##ExpectedStdout: pclose(r)=2
##ExpectedStdout: child got: written by the parent
##ExpectedStdout: pclose(w)=0
##ExpectedStdout: system=5
##ExpectedStdout: system(NULL)=1
set -e
$CC main.c -o main
$CC sh.c -o sh
