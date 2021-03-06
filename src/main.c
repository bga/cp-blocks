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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

//#define _POSIX_C_SOURCE 1
#include <limits.h>

#define OPTION_SPLIT "--split-size="
#define OPTION_STAT "--stat"
#define OPTION_SHOW_MODIFIED_BLOCKS "--show-modified-blocks"
#define OPTION_SHOW_MODIFIED_BLOCKS_SHORT "-m"

const char* const help = ("%s [" OPTION_STAT "] [" OPTION_SHOW_MODIFIED_BLOCKS_SHORT " | " OPTION_SHOW_MODIFIED_BLOCKS "] (srcFile | -) destFile"
	"\ncopy srcFile to destFile but do not overwrite same blocks"
	"\nversion 1.0"
	"\n"
	"\nOptions:"
	"\n\t" OPTION_SPLIT "N(M | G) \tsplit to files destFile.%%03d"
	"\n\t" OPTION_STAT " \toutput statistics"
	"\n\t" OPTION_SHOW_MODIFIED_BLOCKS_SHORT ", " OPTION_SHOW_MODIFIED_BLOCKS " \tdump modified blocks offsets"
);

#define strlen_static(strArg) (sizeof((strArg)) - 1)

enum { bufferSize  = 1024UL * 1024UL };

enum {
	Error_noMemory = -1,  
	Error_srcOpenFailded = -2,  
	Error_destOpenFailded = -3,  
	Error_diskFull = -4,
	Error_commandLineParse = -5,   
};

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
	#define FILE_OFFSET_PRINTF_FORMAT "%08X" 
#else
	typedef uint64_t FileOffset; 
	#define FILE_OFFSET_PRINTF_FORMAT "%016llX" 
#endif

#ifndef O_NOATIME
	//# just ignore
	#define O_NOATIME 0
#endif

int main(int argc, char *argv[]) {
	const char* const selfName = argv[0];
	
	char* commandLineErrorString = NULL;
	int argvFileIndex = 1;
	
	bool isSplit = false;
	int split_index = 0;
	FileOffset split_size = 0;
	
	bool isPrintStat = false;
	bool isShowModofiedBlocks = false;
	
	int ret = 0;

	for(;;) {
		
		if(!(argvFileIndex < argc)) { break; };
		
		if(0) {  }
		else if(strncmp(argv[argvFileIndex], OPTION_SPLIT, strlen_static(OPTION_SPLIT)) == 0) { 
			const char* valueStr = &(argv[argvFileIndex][strlen_static(OPTION_SPLIT)]);
			isSplit = true; 
			
			char postfix;
			FileOffset postfixMultiplier = 1;
			if(sscanf(valueStr, "%u%c", &split_size, &postfix) != 2) {
				commandLineErrorString = strdup("Could not parse --split-size\n");
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
						const char* fmt = "Could not parse --split-size postfix %c\n";
						sprintf((commandLineErrorString = malloc(strlen_static(fmt + 1))), fmt, postfix);
						ret = Error_commandLineParse;
						goto commandLineError;
					}	
				}
				split_size *= postfixMultiplier;
			}
			argvFileIndex += 1; 
		}
		else if(strcmp(argv[argvFileIndex], OPTION_STAT) == 0) { isPrintStat = true; argvFileIndex += 1; }
		else if(strcmp(argv[argvFileIndex], OPTION_SHOW_MODIFIED_BLOCKS_SHORT) == 0 || strcmp(argv[argvFileIndex], OPTION_SHOW_MODIFIED_BLOCKS) == 0) { isShowModofiedBlocks = true; argvFileIndex += 1; }
		else {
			break;
		}
	}
	
	if(!(argvFileIndex + 1 < argc)) { printf(help, selfName); return 0; }
	
	const char* const srcFilePath = argv[argvFileIndex];
	const char* const destFilePathTml = argv[argvFileIndex + 1];
	char destFilePath[PATH_MAX];
	
	uint8_t* srcBuffer;
	uint8_t* destBuffer;

	srcBuffer = malloc(bufferSize + bufferSize); if(srcBuffer == NULL) { ret = Error_noMemory; goto noMemory; }
	destBuffer = &srcBuffer[bufferSize];
	
	int srcFile = ((strcmp(srcFilePath , "-") == 0) ? STDIN_FILENO : open(srcFilePath, O_RDONLY | O_LARGEFILE | O_NOATIME)); if(srcFile < 0) { ret = Error_srcOpenFailded; goto openSrcFailed; }
	int destFile = -1; 

	void openDestFile(int split_index) {
		if(isSplit) {
			sprintf(destFilePath, "%s.%03d", destFilePathTml, split_index);	
		}
		else {
			strcpy(destFilePath, destFilePathTml);
		}
		
		destFile = open(destFilePath, O_RDWR | O_CREAT | O_DSYNC | O_LARGEFILE | O_NOATIME);
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
	unsigned int modifiedBlocksCount = 0;
	FileOffset offset = 0;
	
	for(;;) {
		ssize_t srcReadedBytesCount = read(srcFile, srcBuffer, bufferSize);
		if(srcReadedBytesCount == 0) break;
		ssize_t destReadedBytesCount = read(destFile, destBuffer, srcReadedBytesCount);
		if(srcReadedBytesCount != destReadedBytesCount || memcmp(srcBuffer, destBuffer, srcReadedBytesCount) != 0) {
			lseek(destFile, -destReadedBytesCount, SEEK_CUR);
			if(write(destFile, srcBuffer, srcReadedBytesCount) != srcReadedBytesCount) { ret = Error_diskFull; goto noStorageSpace; }
			modifiedBlocksCount += 1;
			if(isShowModofiedBlocks) {
				fprintf(stderr, "Modifed block " FILE_OFFSET_PRINTF_FORMAT  "\n", offset);
			};
		}
		else {
		}
		blocksCount += 1; 
		offset += srcReadedBytesCount;
		
		if(isSplit && split_size <= offset) {
			closeDestFile();
			
			split_index += 1;
			openDestFile(split_index);
			if(destFile < 0) { ret = Error_destOpenFailded; goto openDestFailed; }
		}
	}
	
	closeDestFile();
	
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
		case(Error_commandLineParse): {
			fprintf(stderr, "%s", commandLineErrorString);
			if(commandLineErrorString != NULL) {
				free(commandLineErrorString);
				commandLineErrorString = NULL;
			};
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
	
	
	return ret;
}
