/*
 * Copyright 2016 - 2017 NXP
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "usb_pd_config.h"
#include "usb_pd.h"
#include "usb_pd_phy.h"
#include "usb_pd_interface.h"
#include "usb_pd_timer.h"
#include "fsl_device_registers.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

void PD_MsgInit(pd_instance_t *pdInstance);
void PD_MsgReceived(pd_instance_t *pdInstance, uint32_t msgLength, pd_status_t result);
void PD_MsgSendDone(pd_instance_t *pdInstance, pd_status_t result);
void PD_StackStateMachine(pd_instance_t *pdInstance);
void PD_ConnectSetPowerProgress(pd_instance_t *pdInstance, uint8_t state);

/*******************************************************************************
 * Variables
 ******************************************************************************/

static pd_instance_t s_PDInstance[PD_CONFIG_MAX_PORT];

#if ((defined PD_CONFIG_PTN5100_PORT) && (PD_CONFIG_PTN5100_PORT))
static const pd_phy_api_interface_t s_PTN5100Interface = {
    PDphy_PTN5100Init, PDphy_PTN5100Deinit, PDphy_PTN5100Send, PDphy_PTN5100Receive, PDphy_PTN5100Control,
};
#endif /* PD_CONFIG_PTN5100_PORT */

#if ((defined PD_CONFIG_PTN5110_PORT) && (PD_CONFIG_PTN5110_PORT))
static const pd_phy_api_interface_t s_PTN5110Interface = {
    PDPTN5110_Init, PDPTN5110_Deinit, PDPTN5110_Send, PDPTN5110_Receive, PDPTN5110_Control,
};
#endif /* PD_CONFIG_PTN5110_PORT */

/*******************************************************************************
 * Code
 ******************************************************************************/
/* 从代码看，one instance <-> one port, 将pd_instance_t类型的变量初始化为0 */
static pd_instance_t *PD_GetInstance(void)
{
    uint8_t i = 0;
    USB_OSA_SR_ALLOC();
    USB_OSA_ENTER_CRITICAL();
    for (; i < PD_CONFIG_MAX_PORT; i++)
    {
		/* 如果该instance没有被occupy，则可以进行初始化 */
        if (s_PDInstance[i].occupied != 1)
        {
            uint8_t *buffer = (uint8_t *)&s_PDInstance[i];
            for (uint32_t j = 0U; j < sizeof(pd_instance_t); j++)
            {
                buffer[j] = 0x00U;
            }
            s_PDInstance[i].occupied = 1;
            USB_OSA_EXIT_CRITICAL();
            return &s_PDInstance[i];
        }
    }
    USB_OSA_EXIT_CRITICAL();
    return NULL;
}

static void PD_ReleaseInstance(pd_instance_t *pdInstance)
{
    USB_OSA_SR_ALLOC();
    USB_OSA_ENTER_CRITICAL();
    pdInstance->occupied = 0;
    USB_OSA_EXIT_CRITICAL();
}

static void PD_GetPhyInterface(uint8_t phyType, const pd_phy_api_interface_t **controllerTable)
{
#if ((defined PD_CONFIG_PTN5100_PORT) && (PD_CONFIG_PTN5100_PORT))
    if (phyType == kPD_PhyPTN5100)
    {
        *controllerTable = &s_PTN5100Interface;
    }
#endif
#if ((defined PD_CONFIG_PTN5110_PORT) && (PD_CONFIG_PTN5110_PORT))
    if (phyType == kPD_PhyPTN5110)
    {
        *controllerTable = &s_PTN5110Interface;
    }
#endif
}

pd_status_t PD_PhyControl(pd_instance_t *pdInstance, uint32_t control, void *param)
{
	// usb_pd_phy.h: pd_phy_control_t enum
    if ((control == PD_PHY_UPDATE_STATE) ||
        (USB_OsaEventCheck(pdInstance->taskEventHandle, PD_TASK_EVENT_PHY_STATE_CHAGNE, NULL) ==
         kStatus_USB_OSA_Success))
    {
        USB_OsaEventClear(pdInstance->taskEventHandle, PD_TASK_EVENT_PHY_STATE_CHAGNE);
		// pd/usb_pd_interface.c中初始化，对应phy中的control函数
        pdInstance->phyInterface->pdPhyControl(pdInstance->pdPhyHandle, PD_PHY_UPDATE_STATE, NULL);
        NVIC_EnableIRQ((IRQn_Type)pdInstance->pdConfig->phyInterruptNum);
    }

    if (control != PD_PHY_UPDATE_STATE)
    {
        return pdInstance->phyInterface->pdPhyControl(pdInstance->pdPhyHandle, control, param);
    }
    else
    {
        return kStatus_PD_Success;
    }
}

//initialize instance
pd_status_t PD_InstanceInit(pd_handle *pdHandle,
                            pd_stack_callback_t callbackFn,
                            pd_power_handle_callback_t *callbackFunctions,
                            void *callbackParam,
                            pd_instance_config_t *config)
{
    pd_instance_t *pdInstance;
    pd_status_t status = kStatus_PD_Success;
	//phy interface configuration, including i2c or spi interface config
    pd_phy_config_t phyConfig;

    pdInstance = PD_GetInstance();
    if (pdInstance == NULL)
    {
        return kStatus_PD_Error;
    }

    /* get phy API table */
    pdInstance->phyInterface = NULL;
	/* get corresponding interface according to phyType */
    PD_GetPhyInterface(config->phyType, &pdInstance->phyInterface);
    if ((pdInstance->phyInterface == NULL) || (pdInstance->phyInterface->pdPhyInit == NULL) ||
        (pdInstance->phyInterface->pdPhyDeinit == NULL) || (pdInstance->phyInterface->pdPhySend == NULL) ||
        (pdInstance->phyInterface->pdPhyReceive == NULL) || (pdInstance->phyInterface->pdPhyControl == NULL))
    {
        PD_ReleaseInstance(pdInstance);
        return kStatus_PD_Error;
    }

	//phyType的类型，PTN5100 or others
    pdInstance->phyType = config->phyType;
	//PD_DpmDemoAppCallback
    pdInstance->pdCallback = callbackFn;
	//callbackFunctions, power related
    pdInstance->callbackFns = callbackFunctions;
	//pd_app_t
    pdInstance->callbackParam = callbackParam;
	//PD REVISION，如果需要改为3.0，需要在usb_pd_config.h中修改
    pdInstance->revision = PD_CONFIG_REVISION;
    pdInstance->sendingMsgHeader.bitFields.specRevision = pdInstance->revision;
    pdInstance->initializeLabel = 0;
	// FreeRTOS event group的等待时间
    pdInstance->waitTime = PD_WAIT_EVENT_TIME;

	// 创建event，句柄保存在pdInstance->taskEventHandle
    if (kStatus_USB_OSA_Success != USB_OsaEventCreate(&(pdInstance->taskEventHandle), 0))
    {
        PD_ReleaseInstance(pdInstance);
        return kStatus_PD_Error;
    }

    /* initialize PHY */
    pdInstance->pdPhyHandle = NULL;
	//instance config
    pdInstance->pdConfig = config;
	//这里的device是指本身还是port partner?
    if (pdInstance->pdConfig->deviceType == kDeviceType_NormalPowerPort)
    {
        pdInstance->pdPowerPortConfig = (pd_power_port_config_t *)pdInstance->pdConfig->deviceConfig;
        pdInstance->pendingSOP = kPD_MsgSOPMask;
    }
    else if (pdInstance->pdConfig->deviceType == kDeviceType_Cable)
    {
        pdInstance->pendingSOP = kPD_MsgSOPpMask | kPD_MsgSOPppMask;
    }
    else
    {
		//audio, debug, alternate mode
        pdInstance->pendingSOP = kPD_MsgSOPMask;
    }
	// phy interface
    phyConfig.interface = config->phyInterface;
    phyConfig.i2cConfig.slaveAddress = config->interfaceParam;
	// phy initialize, call PDPTN5110_Init
    status = pdInstance->phyInterface->pdPhyInit(pdInstance, &(pdInstance->pdPhyHandle), &phyConfig);
    if ((status != kStatus_PD_Success) || (pdInstance->pdPhyHandle == NULL))
    {
        USB_OsaEventDestroy(pdInstance->taskEventHandle);
        PD_ReleaseInstance(pdInstance);
        return kStatus_PD_Error;
    }
    /* usb_pd_msg.c: initialize pd stack */
    PD_MsgInit(pdInstance);
    PD_TimerInit(pdInstance);

    PD_PhyControl(pdInstance, PD_PHY_GET_PHY_VENDOR_INFO, &pdInstance->phyInfo);

    *pdHandle = pdInstance;
    return kStatus_PD_Success;
}

pd_status_t PD_InstanceDeinit(pd_handle pdHandle)
{
    pd_instance_t *pdInstance = (pd_instance_t *)pdHandle;
    pd_status_t status = kStatus_PD_Success;

    USB_OsaEventDestroy(pdInstance->taskEventHandle);
    PD_ReleaseInstance(pdInstance);
    status = pdInstance->phyInterface->pdPhyDeinit(pdInstance->pdPhyHandle);
    PD_ReleaseInstance(pdInstance);

    return status;
}

void PD_InstanceTask(pd_handle pdHandle)
{
    pd_instance_t *pdInstance = (pd_instance_t *)pdHandle;

	// usb_pd_policy.c
    PD_StackStateMachine(pdInstance);
}

// 该函数在phy中被使用，根据phy的变化，通知PE
// 根据event的类型，采取相应的操作。一般是改变相应状态变量，并使用event group进行通知
// 这是一种类似中断的异步机制
void PD_Notify(pd_handle pdHandle, uint32_t event, void *param)
{
    pd_instance_t *pdInstance = (pd_instance_t *)pdHandle;

    switch (event)
    {
        case PD_PHY_EVENT_STATE_CHANGE:
            NVIC_DisableIRQ((IRQn_Type)pdInstance->pdConfig->phyInterruptNum);
            USB_OsaEventSet(pdInstance->taskEventHandle, PD_TASK_EVENT_PHY_STATE_CHAGNE);
            USB_OsaEventSet(pdInstance->taskEventHandle, PD_TASK_EVENT_TYPEC_STATE_PROCESS);
            break;

		//设置相关变量，并设置event group
        case PD_PHY_EVENT_SEND_COMPLETE:
        {
            PD_MsgSendDone(pdInstance, (pd_status_t) * ((uint32_t *)param));
            break;
        }

        case PD_PHY_EVENT_FR_SWAP_SINGAL_RECEIVED:
        {
            PD_ConnectSetPowerProgress(pdInstance, kVbusPower_InFRSwap);
#if 0
            if (!PD_DpmCheckLessOrEqualVsafe5v(pdInstance))
            {
                pdInstance->callbackFns->PD_SrcTurnOffVbus(pdInstance->callbackParam, kVbusPower_Stable);
            }
#endif
            USB_OsaEventSet(pdInstance->taskEventHandle, PD_TASK_EVENT_FR_SWAP_SINGAL);
            break;
        }

        case PD_PHY_EVENT_RECEIVE_COMPLETE:
        {
            pd_phy_rx_result_t *rxResult = (pd_phy_rx_result_t *)param;
            pdInstance->receivedSop = rxResult->rxSop;
            PD_MsgReceived(pdInstance, rxResult->rxLength, (pd_status_t)rxResult->rxResultStatus);
            break;
        }

        case PD_PHY_EVENT_HARD_RESET_RECEIVED:
        {
            PD_ConnectSetPowerProgress(pdInstance, kVbusPower_InHardReset);
            pdInstance->hardResetReceived = 1;
            USB_OsaEventSet(pdInstance->taskEventHandle, PD_TASK_EVENT_RECEIVED_HARD_RESET);
            break;
        }

        case PD_PHY_EVENT_REQUEST_STACK_RESET:
            /* BootSoftRebootToMain(); */
            break;

        case PD_PHY_EVENT_VCONN_PROTECTION_FAULT:

            break;

        case PD_PHY_EVENT_FAULT_RECOVERY:
        {
            pd_phy_config_t phyConfig;

            pdInstance->phyInterface->pdPhyDeinit(pdInstance->pdPhyHandle);
            phyConfig.interface = pdInstance->pdConfig->phyInterface;
            phyConfig.i2cConfig.slaveAddress = pdInstance->pdConfig->interfaceParam;
            pdInstance->phyInterface->pdPhyInit(pdInstance, &(pdInstance->pdPhyHandle), &phyConfig);
            break;
        }

        case PD_PHY_EVENT_VBUS_STATE_CHANGE:
            USB_OsaEventSet(pdInstance->taskEventHandle, PD_TASK_EVENT_OTHER);
            break;

        default:
            break;
    }
}

void USB_WEAK PD_WaitUsec(uint32_t us)
{
    uint32_t usDelay;

    while (us--)
    {
        usDelay = 15;
        while (--usDelay)
        {
            __ASM("nop");
        }
    }
}
