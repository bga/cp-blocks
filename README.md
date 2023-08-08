Smart CLI copy file tool. POSIX compliant. Overwrites only changed clusters in destFile. Supports stdin. Supports destFile spitting.

```
./cp-blocks [options] (srcFile | -) destFile
copy srcFile to destFile but do not overwrite same blocks
version 1.1.2

Options:
        -S, --split-size N(M | G)       split to files destFile.%03d
        -n, --dry-run                   dry run
        -#, --progress                  show progress
        -r, --return-true-if-modified   return true if modified
        -s, --stat                      output statistics
        -m, --show-modified-blocks      dump modified blocks offsets
```
