NR == FNR { want[n++] = $0; next }
i < n && index($0, want[i]) { i++ }
END {
    if (i < n) {
        print "missing: " want[i] > "/dev/stderr"
        exit 1
    }
}
