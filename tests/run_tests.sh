#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHELL_BIN="$REPO_ROOT/posix-shell"

if [ ! -x "$SHELL_BIN" ]; then
    echo "Error: $SHELL_BIN not found or not executable. Run 'make' first."
    exit 1
fi

TMP_BASE="$(mktemp -d)"
trap 'rm -rf "$TMP_BASE"' EXIT

WORK_DIR="$TMP_BASE/work"
TEST_HOME="$TMP_BASE/home"
mkdir -p "$WORK_DIR/sub/nested" "$TEST_HOME"
touch "$WORK_DIR/.hidden_file" "$WORK_DIR/alpha.txt" "$WORK_DIR/sub/nested/target.txt"
printf "cherry\napple\nbanana\n" > "$WORK_DIR/fruits.txt"

PASS=0
FAIL=0

run_shell() {
    local input="$1"
    (
        cd "$WORK_DIR"
        HOME="$TEST_HOME" printf "%b" "$input" | "$SHELL_BIN" 2>&1
    )
}

assert_contains() {
    local name="$1"
    local output="$2"
    local expected="$3"
    if printf "%s" "$output" | grep -qF -- "$expected"; then
        echo "  [PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] $name"
        echo "         Expected to find: $expected"
        echo "         Actual output:"
        printf "%s\n" "$output" | sed 's/^/           /'
        FAIL=$((FAIL + 1))
    fi
}

assert_regex() {
    local name="$1"
    local output="$2"
    local pattern="$3"
    if printf "%s" "$output" | grep -qE -- "$pattern"; then
        echo "  [PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] $name"
        echo "         Expected regex: $pattern"
        echo "         Actual output:"
        printf "%s\n" "$output" | sed 's/^/           /'
        FAIL=$((FAIL + 1))
    fi
}

echo "Running POSIX Shell test suite..."

# 1. Built-ins: pwd & echo
OUT="$(run_shell "pwd\necho hello   posix   shell\nexit\n")"
assert_contains "pwd prints working directory" "$OUT" "$WORK_DIR"
assert_contains "echo prints arguments" "$OUT" "hello posix shell"

# 2. Built-in: cd (subdir, -, .., ~, invalid args)
OUT="$(run_shell "cd sub\npwd\ncd -\npwd\ncd sub/nested\ncd ~\npwd\ncd a b\nexit\n")"
assert_contains "cd into subdirectory" "$OUT" "$WORK_DIR/sub"
assert_contains "cd - returns to previous dir" "$OUT" "$WORK_DIR"
assert_contains "cd with too many args reports error" "$OUT" "Invalid arguments"

# 3. Built-in: ls (-a, -l, -la)
OUT="$(run_shell "ls\nls -a\nls -l alpha.txt\nexit\n")"
assert_contains "ls lists regular file" "$OUT" "alpha.txt"
assert_contains "ls -a lists hidden file" "$OUT" ".hidden_file"
assert_regex "ls -l shows permissions and filename" "$OUT" "[-rwx]{10}.*alpha\.txt"

# 4. Built-in: recursive search
OUT="$(run_shell "search target.txt\nsearch does_not_exist_12345\nexit\n")"
assert_contains "search finds nested file" "$OUT" "True"
assert_contains "search reports missing file" "$OUT" "False"

# 5. Semicolon-separated command chaining
OUT="$(run_shell "echo first; echo second; echo third\nexit\n")"
assert_contains "semicolon chaining (1st)" "$OUT" "first"
assert_contains "semicolon chaining (2nd)" "$OUT" "second"
assert_contains "semicolon chaining (3rd)" "$OUT" "third"

# 6. I/O Redirection (>, >>, <, combined)
OUT="$(run_shell "echo line1 > redir.txt\necho line2 >> redir.txt\ncat < redir.txt\ncat < redir.txt > redir_copy.txt\ncat redir_copy.txt\nexit\n")"
assert_contains "output & input redirection (> and <)" "$OUT" "line1"
assert_contains "append redirection (>>)" "$OUT" "line2"
if [ -f "$WORK_DIR/redir_copy.txt" ] && grep -q "line2" "$WORK_DIR/redir_copy.txt"; then
    echo "  [PASS] combined input/output redirection (< and >)"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] combined input/output redirection (< and >)"
    FAIL=$((FAIL + 1))
fi

# 7. Multi-stage pipelines and pipeline + redirection
OUT="$(run_shell "cat fruits.txt | sort | head -n 1\ncat fruits.txt | sort | wc -l > count.txt\ncat count.txt\nexit\n")"
assert_contains "3-stage pipeline (cat | sort | head)" "$OUT" "apple"
assert_regex "pipeline with output redirection" "$OUT" "(^|[[:space:]])3([[:space:]]|$)"

# 8. Background execution (&)
OUT="$(run_shell "sleep 0.2 &\nexit\n")"
assert_regex "background command prints child PID" "$OUT" "[0-9]+"

# 9. Persistent history across sessions & argument validation
rm -f "$TEST_HOME/.shell_history"
run_shell "echo hist_one\necho hist_two\nexit\n" >/dev/null
OUT="$(run_shell "history 2\nhistory invalid_arg\nexit\n")"
assert_contains "persistent history retains prior session" "$OUT" "exit"
assert_contains "history rejects invalid numeric argument" "$OUT" "Invalid arguments"

# 10. Error handling
OUT="$(run_shell "nonexistent_cmd_xyz_987\ncat < nonexistent_input_file_987.txt\nexit\n")"
assert_contains "unknown command prints error" "$OUT" "command not found"
assert_regex "missing input redirection file prints error" "$OUT" "No such file|cannot open|open"

# 11. Linux-specific pinfo (/proc) check
if [ "$(uname -s)" = "Linux" ]; then
    OUT="$(run_shell "pinfo\nexit\n")"
    assert_contains "pinfo reports Process Status on Linux" "$OUT" "Process Status --"
    assert_contains "pinfo reports memory on Linux" "$OUT" "memory --"
    assert_contains "pinfo reports Executable Path on Linux" "$OUT" "Executable Path --"
else
    echo "  [SKIP] pinfo /proc checks (Linux-only; current OS: $(uname -s))"
fi

echo ""
echo "Test Summary: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
