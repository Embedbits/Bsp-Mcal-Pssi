/**
 * \author Mr.Nobody
 * \file Pssi_Reg.h
 * \ingroup Pssi
 * \brief Pssi module Reg component header file.
 *
 * This component contains functionality for register operations. It is private
 * to the module (the register access is not shared with other modules).
 *
 */

#ifndef PSSI_PSSI_REG_H
#define PSSI_PSSI_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================= INCLUDES =================================== */
#include "Pssi_Types.h"                      /* Module types definition        */
/* ============================= TYPEDEFS =================================== */

/** \brief List of PSSI registers used by the module (PSSI_DR is read/written only by the DMA,
 *  it is never touched by software - its address is used directly as the Gpdma source or
 *  destination address). PSSI_SR (RTT1B/RTT4B) is not used: the module is DMA only, it never
 *  polls the FIFO fill level itself. */
typedef enum
{
    PSSI_REG_CR     = 0u, /**< PSSI control register                        */
    PSSI_REG_RIS        , /**< PSSI raw interrupt status register (read only) */
    PSSI_REG_IER        , /**< PSSI interrupt enable register               */
    PSSI_REG_ICR        , /**< PSSI interrupt clear register (write only)   */
    PSSI_REG_CNT          /**< Count of available PSSI registers            */
}   pssi_RegId_t;

/* ========================= SYMBOLIC CONSTANTS ============================= */

/* ========================= EXPORTED MACROS ================================ */

/* ========================= EXPORTED VARIABLES ============================= */

/* ======================== EXPORTED FUNCTIONS ============================== */

/* ---------------------------- Generic accessors --------------------------- */

void                        Pssi_Set_RegBit             ( pssi_RegId_t regId, uint32_t bitMask );
void                        Pssi_Reset_RegBit           ( pssi_RegId_t regId, uint32_t bitMask );
uint32_t                    Pssi_Get_RegBit             ( pssi_RegId_t regId, uint32_t bitMask );

void                        Pssi_Set_RegVal             ( pssi_RegId_t regId, uint32_t regMask, uint32_t regValue );
uint32_t                    Pssi_Get_RegVal             ( pssi_RegId_t regId, uint32_t regMask );

uint32_t                    Pssi_Reg_Get_DataRegAddr    ( void );

/* ----------------------------- Register protocol -------------------------- */

pssi_RequestState_t         Pssi_Reg_Set_Enable         ( void );
pssi_RequestState_t         Pssi_Reg_Set_Disable        ( void );
pssi_FunctionState_t        Pssi_Reg_Get_EnableState     ( void );

#ifdef __cplusplus
}
#endif

#endif /* PSSI_PSSI_REG_H */
