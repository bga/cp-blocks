/*
  Copyright 2020 Bga <bga.email@gmail.com>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <getopt.h>

//#define _POSIX_C_SOURCE 1
#include <limits.h>

#define MALLOC_TYPE(typeArg) ((typeArg*)malloc(sizeof(typeArg)))

#define OPTION_SPLIT "split-size"
#define OPTION_SPLIT_SHORT "S"
#define OPTION_DRY_RUN "dry-run"
#define OPTION_DRY_RUN_SHORT "n"
#define OPTION_SHOW_PROGRESS "progress"
#define OPTION_SHOW_PROGRESS_SHORT "#"
#define OPTION_RET_TRUE_IF_MODIFIED "return-true-if-modified"
#define OPTION_RET_TRUE_IF_MODIFIED_SHORT "r"
#define OPTION_STAT "stat"
#define OPTION_STAT_SHORT "s"
#define OPTION_SHOW_MODIFIED_BLOCKS "show-modified-blocks"
#define OPTION_SHOW_MODIFIED_BLOCKS_SHORT "m"

const char help[] = ("%s [options] (srcFile | -) (destFile | destDir/)"
	"\ncopy srcFile to destFile but do not overwrite same blocks"
	"\nif destDir passed then destFile = destDir + basename(srcFile)"
	"\nversion " VERSION
	"\n"
	"\nOptions:"
	"\n\t" "-" OPTION_SPLIT_SHORT ", " "--" OPTION_SPLIT " N(M | G) \tsplit to files destFile.%%03d"
	"\n\t" "-" OPTION_DRY_RUN_SHORT ", " "--" OPTION_DRY_RUN " \t\t\tdry run"
	"\n\t" "-" OPTION_SHOW_PROGRESS_SHORT ", " "--" OPTION_SHOW_PROGRESS " \t\t\tshow progress"
	"\n\t" "-" OPTION_RET_TRUE_IF_MODIFIED_SHORT ", " "--" OPTION_RET_TRUE_IF_MODIFIED " \treturn true if modified"
	"\n\t" "-" OPTION_STAT_SHORT ", " "--" OPTION_STAT " \t\t\toutput statistics"
	"\n\t" "-" OPTION_SHOW_MODIFIED_BLOCKS_SHORT ", " "--" OPTION_SHOW_MODIFIED_BLOCKS " \tdump modified blocks offsets"
	"\n"
);

#define strlen_static(strArg) (sizeof((strArg)) - 1)

enum { bufferSize  = 1024UL * 1024UL };

enum {
	Error_noMemory = -1,  
	Error_srcOpenFailded = -2,  
	Error_destOpenFailded = -3,  
	Error_diskFull = -4,
	Error_commandLineParse = -5,   
	Error_ioGenericFailure = -6,   
	
	Error_notModified = 1,
};

bool Path_isDir(char const* path) {
	if(path == NULL || *path == 0) return false;
	return path[strlen(path) - 1] == '/';
}
char const* Path_basename(char const* path) {
	char const* slashPtr = strrchr(path, '/');
	if(slashPtr == NULL) slashPtr = path;
	return slashPtr;
}
bool File_isDir(char const* path) {
	struct stat statbuf;
	if(stat(path, &statbuf) != 0) return 0;
	return S_ISDIR(statbuf.st_mode);
}

int File_eof(int fd) {
	uint8_t buffer[1];
	int readedBytesCount = read(fd, buffer, 1);
	if(0 < readedBytesCount) {
		lseek(fd, -readedBytesCount, SEEK_CUR);
		return 0;
	}
	else {
		return 1;
	}
}
int File_truncate(int fd) {
#ifdef _WIN32
	return _chsize_s(fd, ftello64(fd));
#else
	return ftruncate(fd, lseek(fd, 0,  SEEK_CUR));
#endif
}



#ifndef O_LARGEFILE
	#warning No large file support
	#define O_LARGEFILE 0
	typedef uint32_t FileOffset;
	#define FILE_OFFSET_PRINTF_FORMAT "%08"PRIX32 
	#define FILE_OFFSET_SCANF_FORMAT "%"SCNu32

#else
	typedef uint64_t FileOffset; 
	#define FILE_OFFSET_PRINTF_FORMAT "%016"PRIX64
	#define FILE_OFFSET_SCANF_FORMAT "%"SCNu64
#endif

#ifndef O_NOATIME
	//# just ignore
	#define O_NOATIME 0
#endif

int File_open(char const* path, int flags, mode_t mode) {
	int f = open(path, flags, mode);
	#if O_NOATIME != 0
		if(f < 0) f = open(path, flags & ~O_NOATIME, mode);
	#endif
	return f;
}

ssize_t File_read(int file, uint8_t* buffer, size_t bufferSize) {
	size_t bufferOffset = 0;
	
	for(;;) {	
		ssize_t readedBytesCount = read(file, &(buffer[bufferOffset]), bufferSize - bufferOffset);
		//# error?
		if(readedBytesCount < 0) return readedBytesCount;
		//# eof?
		if(readedBytesCount == 0) break;
		bufferOffset += readedBytesCount;
		if(bufferSize <= bufferOffset) break;
	}
	
	return bufferOffset;
}

typedef struct File_AsyncRequest {
	pthread_t thread;
} File_AsyncRequest;
struct File_readAnync_thread_Request {
	int file; uint8_t* buffer; size_t bufferSize;
};
struct File_readAnync_thread_Result {
	ssize_t bytesCount;
};
ssize_t File_AsyncRequest_wait(File_AsyncRequest asyncRequest) {
	struct File_readAnync_thread_Result* retPtr = NULL;
	
	pthread_join(asyncRequest.thread, (void**)&retPtr);
	if(retPtr == NULL) return -1;
	ssize_t bytesCount = retPtr->bytesCount;
	free((void*)retPtr);
	return bytesCount;
}
static void* File_readAnync_thread(void* dataPtr) {
	struct File_readAnync_thread_Request data = *(struct File_readAnync_thread_Request*)dataPtr;
	free(dataPtr);
	struct File_readAnync_thread_Result* retPtr = MALLOC_TYPE(struct File_readAnync_thread_Result);
	retPtr ->bytesCount = File_read(data.file, data.buffer, data.bufferSize);
	
	return (void*)retPtr;
}
File_AsyncRequest File_readAnync(int file, uint8_t* buffer, size_t bufferSize) {
	struct File_readAnync_thread_Request* dataPtr = MALLOC_TYPE(struct File_readAnync_thread_Request);
	
	dataPtr->file = file;
	dataPtr->buffer = buffer;
	dataPtr->bufferSize = bufferSize;
	
	File_AsyncRequest asyncRequest; pthread_create(&(asyncRequest.thread), NULL, File_readAnync_thread, (void *)dataPtr);
	
	return asyncRequest;
}

char* rstrcat(char* dest, char const* src) {
	size_t destLen = strlen(dest);
	size_t srcLen = strlen(src);
	dest = realloc(dest, destLen + srcLen + 1);
	if(dest == NULL) return dest;
	strcat(dest + destLen, src);

	return dest;
}

int main(int argc, char *argv[]) {
	const char* const selfName = argv[0];
	
	char* commandLineErrorString = NULL;
	int argvFileIndex = 1;
	
	bool isSplit = false;
	int split_index = 0;
	FileOffset split_size = 0;
	
	bool isDryRun = false;
	bool isPrintStat = false;
	bool isShowProgress = false;
	bool isShowModofiedBlocks = false;
	bool isRetTrueIfModified = false;
	
	unsigned int modifiedBlocksCount = 0;
	
	int ret = 0;

	
	for(;;) {
		static struct option long_options[] = {
			{ OPTION_SPLIT, required_argument, NULL, OPTION_SPLIT_SHORT[0] },
			{ OPTION_DRY_RUN, no_argument, NULL, OPTION_DRY_RUN_SHORT[0] },
			{ OPTION_SHOW_PROGRESS, no_argument, NULL, OPTION_SHOW_PROGRESS_SHORT[0] },
			{ OPTION_RET_TRUE_IF_MODIFIED, no_argument, NULL, OPTION_RET_TRUE_IF_MODIFIED_SHORT[0] },
			{ OPTION_STAT, no_argument, NULL, OPTION_STAT_SHORT[0] },
			{ OPTION_SHOW_MODIFIED_BLOCKS, no_argument, NULL, OPTION_SHOW_MODIFIED_BLOCKS_SHORT[0] },
			{0, 0, 0, 0}
		};
		
		static char const options_short[] = (
			OPTION_SPLIT_SHORT ":"
			OPTION_DRY_RUN_SHORT
			OPTION_SHOW_PROGRESS_SHORT
			OPTION_RET_TRUE_IF_MODIFIED_SHORT
			OPTION_STAT_SHORT
			OPTION_SHOW_MODIFIED_BLOCKS_SHORT
		);

		
		//# getopt_long stores the option index here
		// int option_index = 0;
		
		int c = getopt_long(argc, argv, options_short, long_options, NULL);
		
		//# sorry C string literal is not constant so i can not use { switch(c) case OPTION_DRY_RUN_SHORT[0]: }
		if(0) {  }
		else if(c == -1) {
			break;
		}
		else if(c == OPTION_SPLIT_SHORT[0]) { 
			const char* valueStr = optarg;

			isSplit = true;
		
			char postfix;
			FileOffset postfixMultiplier = 1;
			if(sscanf(valueStr, FILE_OFFSET_SCANF_FORMAT "%c", &split_size, &postfix) != 2) {
				commandLineErrorString = strdup("Could not parse --" OPTION_SPLIT "\n");
				ret = Error_commandLineParse;
				goto commandLineError;
			}
			else {
				switch(postfix) {
					case('M'): {
						postfixMultiplier = 1024 * 1024;
					} break;
					case('G'): {
						postfixMultiplier = 1024 * 1024 * 1024;
					} break;
					default: {
						const char fmt[] = "Could not parse --" OPTION_SPLIT " postfix %c\n";
						sprintf((commandLineErrorString = malloc(strlen_static(fmt) + 1)), fmt, postfix);
						ret = Error_commandLineParse;
						goto commandLineError;
					}	
				}
				split_size *= postfixMultiplier;
			}

		} 
		
		else if(c == OPTION_DRY_RUN_SHORT[0]) { isDryRun = true; }
		else if(c == OPTION_SHOW_PROGRESS_SHORT[0]) { isShowProgress = true; }
		else if(c == OPTION_RET_TRUE_IF_MODIFIED_SHORT[0]) { isRetTrueIfModified = true; }
		else if(c == OPTION_STAT_SHORT[0]) { isPrintStat = true; }
		else if(c == OPTION_SHOW_MODIFIED_BLOCKS_SHORT[0]) { isShowModofiedBlocks = true; }
		
		else if(c == '?') {
			ret = Error_commandLineParse;
			goto commandLineError;
			//# getopt_long already printed an error message
		} 
			
		else {
			ret = Error_commandLineParse;
			goto commandLineError;
		}
	}
	argvFileIndex = optind;

	if(argc <= argvFileIndex) { 
		commandLineErrorString = strdup("Missed srcFile and destFile\n");
		ret = Error_commandLineParse;
		goto commandLineError;
	};
	if(argc - 1 <= argvFileIndex) { 
		commandLineErrorString = strdup("Missed destFile\n");
		ret = Error_commandLineParse;
		goto commandLineError;
	};
	
	const char* const srcFilePath = argv[argvFileIndex];
	const char* destFilePathTml = argv[argvFileIndex + 1];
	char destFilePath[PATH_MAX];
	
	uint8_t* srcBuffer;
	uint8_t* destBuffer;

	srcBuffer = malloc(bufferSize + bufferSize); if(srcBuffer == NULL) { ret = Error_noMemory; goto noMemory; }
	destBuffer = &srcBuffer[bufferSize];
	
	int srcFile = ((strcmp(srcFilePath , "-") == 0) ? STDIN_FILENO : File_open(srcFilePath, O_RDONLY | O_LARGEFILE | O_NOATIME, 0)); if(srcFile < 0) { ret = Error_srcOpenFailded; goto openSrcFailed; }

	//# if { destFilePath } is dir - make { destFilePath += basename(srcFilePath)
	if(Path_isDir(destFilePathTml)) {
		if(!File_isDir(destFilePathTml)) { ret = Error_destOpenFailded; goto openDestFailed; }
		//# ok unreleased ptr...
		destFilePathTml = rstrcat(strdup(destFilePathTml), Path_basename(srcFilePath));
	};
	int destFile = -1; 

	void openDestFile(int split_index) {
		if(isSplit) {
			sprintf(destFilePath, "%s.%03d", destFilePathTml, split_index);	
		}
		else {
			strcpy(destFilePath, destFilePathTml);
		}
		
		destFile = File_open(destFilePath, O_RDWR | O_CREAT | O_DSYNC | O_LARGEFILE | O_NOATIME, 0666);
	};
	void closeDestFile() {
		if(destFile < 0) return;
		
		if(!File_eof(destFile)) {
			File_truncate(destFile);
		};
		close(destFile);
		destFile = -1;
	}

	openDestFile(split_index);
	if(destFile < 0) { ret = Error_destOpenFailded; goto openDestFailed; }
	
	unsigned int blocksCount = 0;
	FileOffset offset = 0;
	FileOffset nextSplitSizeBound = split_size;
	
	for(;;) {
		File_AsyncRequest srcReadAsyncRequest = File_readAnync(srcFile, srcBuffer, bufferSize);
		File_AsyncRequest destReadAsyncRequest = File_readAnync(destFile, destBuffer, bufferSize);
		ssize_t srcReadedBytesCount = File_AsyncRequest_wait(srcReadAsyncRequest);
		ssize_t destReadedBytesCount = File_AsyncRequest_wait(destReadAsyncRequest);
		if(srcReadedBytesCount < 0 || destReadedBytesCount < 0) { ret = Error_ioGenericFailure; goto ioError; }
		lseek(destFile, -destReadedBytesCount, SEEK_CUR);
		
		if(srcReadedBytesCount == 0) break;
		if(srcReadedBytesCount != destReadedBytesCount || memcmp(srcBuffer, destBuffer, srcReadedBytesCount) != 0) {
			if(isDryRun) {
				lseek(destFile, srcReadedBytesCount, SEEK_CUR);
			}
			else if(write(destFile, srcBuffer, srcReadedBytesCount) != srcReadedBytesCount) { ret = Error_diskFull; goto noStorageSpace; }
			modifiedBlocksCount += 1;
			if(isShowModofiedBlocks) {
				fprintf(stderr, "Modifed block " FILE_OFFSET_PRINTF_FORMAT  "\n", offset);
			};
		}
		else {
			lseek(destFile, destReadedBytesCount, SEEK_CUR);
		}
		blocksCount += 1; 
		offset += srcReadedBytesCount;
		
		if(isShowProgress) {
			static int statusStringLen = 0; 
			
			//# clear last output
			fputc('\r', stderr);
			while(statusStringLen--) {
				fputc(' ', stderr);
			}
			
			statusStringLen = fprintf(stderr, "\r%s %luM", destFilePath, (bufferSize  / 1024UL / 1024UL) * blocksCount);
		};

		if(isSplit && nextSplitSizeBound <= offset) {
			nextSplitSizeBound += split_size;
			closeDestFile();
			
			split_index += 1;
			openDestFile(split_index);
			if(destFile < 0) { ret = Error_destOpenFailded; goto openDestFailed; }
		}
	}
	
	closeDestFile();
	
	if(isShowProgress) {
		fprintf(stderr, "\n");
	};

	if(isPrintStat) {
		fprintf(stderr, 
			"Stat:" 
			"\n\tblocks total: %u"
			"\n\tblocks modified: %u"
			"\n\tblocks modified ratio: %.2lf%%"
			"\n", 
			blocksCount, modifiedBlocksCount, ((double)modifiedBlocksCount) / blocksCount * 100
		);
	};
	
	noStorageSpace:
	ioError:
	
	close(destFile);
	openDestFailed:
	
	close(srcFile);
	openSrcFailed:
	
	free(srcBuffer);
	
	noMemory:
	commandLineError:
	
	switch(ret) {
		case(0): {
			//# ok
		} break;
		case(Error_ioGenericFailure): {
			fprintf(stderr, "Generic io error\n");
		} break;
		case(Error_commandLineParse): {
			if(commandLineErrorString != NULL) {
				fprintf(stderr, "%s", commandLineErrorString);
				free(commandLineErrorString);
				commandLineErrorString = NULL;
				
			};
			printf(help, selfName);
		} break;
		case(Error_noMemory): {
			fprintf(stderr, "Could not allocate memory for buffer\n");
		} break;
		case(Error_srcOpenFailded): {
			fprintf(stderr, "Could not open srcFile %s\n", srcFilePath);
		} break;
		case(Error_destOpenFailded): {
			fprintf(stderr, "Could not open destFile %s\n", destFilePath);
		} break;
		case(Error_diskFull): {
			fprintf(stderr, "Could not write to destFile %s. No disk space\n", destFilePath);
		} break;
		default: {
			fprintf(stderr, "Could not allocate memory for buffer\n");
		}
	}
	
	if(isRetTrueIfModified && ret == 0 && modifiedBlocksCount == 0) {
		ret = Error_notModified;
	};
	
	return ret;
}
