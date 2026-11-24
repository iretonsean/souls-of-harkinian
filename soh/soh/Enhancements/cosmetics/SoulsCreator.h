#pragma once

#include <stdbool.h>
#include <stdint.h>

// Souls of Harkinian character creator in the file select (see SoulsCreator.cpp).
#ifdef __cplusplus
extern "C" {
#endif
bool SoulsCreator_IsEnabled(void);
bool SoulsCreator_IsActive(void);
void SoulsCreator_Begin(void);
void SoulsCreator_End(void);
int16_t* SoulsCreator_UIAlpha(void);
uint16_t* SoulsCreator_ArrowOffset(void);
void SoulsCreator_Update(void* gameState);       // GameState* of the file select
void SoulsCreator_Draw(void* fileChooseContext); // FileChooseContext*
#ifdef __cplusplus
}
#endif
