/**
 * \author Mr.Nobody
 * \file Pssi.h
 * \ingroup Pssi
 * \brief Pssi module private declarations
 *
 * Internal state and helpers of the module, not part of the public interface
 * (Pssi_Port.h). Shared between Pssi.c and Pssi_Reg.c.
 *
 */

#ifndef PSSI_PSSI_H
#define PSSI_PSSI_H
/* ============================== INCLUDES ================================== */
#include "Pssi_Port.h"                       /* Own port file include          */
/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Number of loop iterations of a bounded wait on a register flag before a timeout is declared */
#define PSSI_REG_WAIT_LOOP_CNT                   ( 100000u )

/** Largest single transfer size (bytes) accepted by Pssi_Set_RxStart / Pssi_Set_TxStart (the
 *  gpdma_BlockSize_t range, single GPDMA block, no chaining - see the module README "Not part
 *  of the first version") */
#define PSSI_XFER_SIZE_MAX                        ( 65535u )

/* ============================== TYPEDEFS ================================== */

/** Run-time state of the PSSI instance */
typedef struct
{
    pssi_FunctionState_t Initialized;    /**< Instance configured by Pssi_Init              */
    pssi_Config_t        Config;         /**< Copy of the configuration passed to Pssi_Init */
    pssi_XferState_t     XferState;      /**< Current transfer state                        */
    pssi_XferError_t     LastError;      /**< Error mask of the last failed transfer        */
    pssi_DataCnt_t       XferByteCnt;    /**< Size (bytes) of the transfer in progress      */
    pssi_FunctionState_t DmaChannelInit; /**< Gpdma channel currently configured            */
}   pssi_InstanceData_t;

/* ========================= EXPORTED VARIABLES ============================= */
/* ======================== EXPORTED FUNCTIONS ============================== */

pssi_RequestState_t    PssiPriv_Get_Instance          ( pssi_InstanceData_t ** const instance );

#endif /* PSSI_PSSI_H */
