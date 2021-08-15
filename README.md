Smart CLI copy file tool. POSIX compliant. Overwrites only changed clusters in destFile. Supports stdin

```
cp-blocks [--stat] [-m | --show-modified-blocks] (srcFile | -) destFile
copy srcFile to destFile but do not overwrite same blocks
version 1.0

Options:
	--split-size=N(M | G) 	split to files destFile.%03d
	--dry-run 	dry run
	--progress 	show progress
	--stat 	output statistics
	-m, --show-modified-blocks 	dump modified blocks offsets
```
