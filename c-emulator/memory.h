/* Raw access to the existing VLM memory manager, bypassing the stack cache. */
#ifndef C_EMULATOR_MEMORY_H
#define C_EMULATOR_MEMORY_H
#include "emulator.h"
int CRawRead(Integer vma, CObject *value);
int CRawWrite(Integer vma, CObject *value);
int CRawReadBlock(Integer vma, CObject *value, int count);
int CRawWriteBlock(Integer vma, CObject *value, int count);
#endif
