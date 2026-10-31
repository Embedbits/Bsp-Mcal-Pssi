/**
 * \author Mr.Nobody
 * \file Pssi_Port.h
 * \ingroup Pssi
 * \brief Pssi module public interface header file
 *
 * This file contains the public interface of the module, identical for all
 * supported families. Include this file to use the module from an application
 * or from another module. Every function is documented at its definition in
 * Pssi.c (on STM32H503/H543/H553, which have no PSSI peripheral, every function
 * only returns PSSI_REQUEST_ERROR).
 *
 */

#ifndef PSSI_PSSI_PORT_H
#define PSSI_PSSI_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= INCLUDES =================================== */
#include "Pssi_Types.h"                      /* Module types definition        */
/* ============================= TYPEDEFS =================================== */
/* ========================= SYMBOLIC CONSTANTS ============================= */
/* ========================= EXPORTED MACROS ================================ */
/* ========================= EXPORTED VARIABLES ============================= */
/* ======================== EXPORTED FUNCTIONS ============================== */

/* --------------------------- Module management ----------------------------- */

pssi_ModuleVersion_t    Pssi_Get_ModuleVersion     ( void );

pssi_RequestState_t     Pssi_Init                  ( const pssi_Config_t * const pssiConfig );
pssi_RequestState_t     Pssi_Deinit                ( void );
void                    Pssi_Task                  ( void );

pssi_RequestState_t     Pssi_Get_DefaultConfig     ( pssi_Config_t * const pssiConfig );

/* -------------------------------- Transfers ---------------------------------- */

pssi_RequestState_t     Pssi_Set_RxStart           ( pssi_Data_t * const buffer, pssi_DataCnt_t byteCnt );
pssi_RequestState_t     Pssi_Set_TxStart           ( const pssi_Data_t * const buffer, pssi_DataCnt_t byteCnt );
pssi_RequestState_t     Pssi_Set_Abort             ( void );
pssi_RequestState_t     Pssi_Get_XferState         ( pssi_XferState_t * const xferState );
pssi_RequestState_t     Pssi_Get_XferCount         ( pssi_DataCnt_t * const transferredCnt );
pssi_RequestState_t     Pssi_Get_XferError         ( pssi_XferError_t * const errorMask );

/* --------------------------------- Settings ---------------------------------- */

pssi_RequestState_t     Pssi_Set_DataWidth         ( pssi_DataWidth_t dataWidth );
pssi_RequestState_t     Pssi_Get_DataWidth         ( pssi_DataWidth_t * const dataWidth );

pssi_RequestState_t     Pssi_Set_ClockEdge         ( pssi_ClockEdge_t clockEdge );
pssi_RequestState_t     Pssi_Get_ClockEdge         ( pssi_ClockEdge_t * const clockEdge );

pssi_RequestState_t     Pssi_Set_Handshake         ( pssi_Handshake_t handshake,
                                                     pssi_Polarity_t dePolarity,
                                                     pssi_Polarity_t rdyPolarity );
pssi_RequestState_t     Pssi_Get_Handshake         ( pssi_Handshake_t * const handshake,
                                                      pssi_Polarity_t * const dePolarity,
                                                      pssi_Polarity_t * const rdyPolarity );

/* -------------------------------- Interrupts --------------------------------- */

pssi_RequestState_t     Pssi_Set_IrqPriority       ( pssi_IrqPrio_t irqPriority );
pssi_RequestState_t     Pssi_Get_IrqPriority       ( pssi_IrqPrio_t * const irqPriority );

/* ------------------------------------ Pins ----------------------------------- */

pssi_RequestState_t     Pssi_InitClockGpio         ( pssi_ClockPin_t clockPin );
pssi_RequestState_t     Pssi_InitDataGpio          ( pssi_DataPin_t dataPin );
pssi_RequestState_t     Pssi_InitDeGpio            ( pssi_DePin_t dePin );
pssi_RequestState_t     Pssi_InitRdyGpio           ( pssi_RdyPin_t rdyPin );

#ifdef __cplusplus
}
#endif

#endif /* PSSI_PSSI_PORT_H */
