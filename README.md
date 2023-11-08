Smart CLI copy file tool. POSIX compliant. Overwrites only changed clusters in destFile. Supports stdin. Supports destFile spitting.

```
./cp-blocks [options] (srcFile | -) (destFile | destDir/)
copy srcFile to destFile but do not overwrite same blocks
if destDir passed then destFile = destDir + basename(srcFile)
version 1.1.5

Options:
	-S, --split-size N(M | G) 	split to files destFile.%03d
	-n, --dry-run 			dry run
	-#, --progress 			show progress
	-r, --return-true-if-modified 	return true if modified
	-s, --stat 			output statistics
	-m, --show-modified-blocks 	dump modified blocks offsets
```
