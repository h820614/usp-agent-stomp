/*
 *
 * Copyright (C) 2019, Broadband Forum
 * Copyright (C) 2016-2019  ARRIS Enterprises, LLC
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * \file device_mtp.c
 *
 * Implements the Device.LocalAgent.MTP data model object
 *
 */

#include <time.h>
#include <string.h>
#include <sys/socket.h>

#include "common_defs.h"
#include "data_model.h"
#include "usp_api.h"
#include "dm_access.h"
#include "dm_trans.h"
#include "mtp_exec.h"
#include "device.h"
#include "text_utils.h"

#ifdef ENABLE_COAP
#include "usp_coap.h"
#endif

//------------------------------------------------------------------------------
// Location of the local agent MTP table within the data model
#define DEVICE_AGENT_MTP_ROOT "Device.LocalAgent.MTP"
static const char device_agent_mtp_root[] = DEVICE_AGENT_MTP_ROOT;

//------------------------------------------------------------------------------
// Structure representing entries in the Device.LocalAgent.MTP.{i} table
typedef struct
{
    int instance;      // instance of the MTP in the Device.LocalAgent.MTP.{i} table
    bool enable;
    mtp_protocol_t protocol;    

    // NOTE: The following parameters are not a union because the data model allows us to setup both STOMP and CoAP params at the same time, and just select between them using the protocol parameter
    int stomp_connection_instance; // Instance number of the STOMP connection which this MTP refers to (ie Device.STOMP.Connection.{i})
    char *stomp_agent_queue;    // name of the queue on the above STOMP connection, on which this agent listens

#ifdef ENABLE_COAP
    unsigned coap_port;         // port on which this agent listens for CoAP messages
    char *coap_path;            // path representing this agent
#endif
} agent_mtp_t;

// Array of agent MTPs
static agent_mtp_t agent_mtps[MAX_AGENT_MTPS];

//------------------------------------------------------------------------------
// Table used to convert from a textual representation of an MTP protocol to an enumeration
const enum_entry_t mtp_protocols[kMtpProtocol_Max] = 
{
    { kMtpProtocol_None, "" },
    { kMtpProtocol_STOMP, "STOMP" },
#ifdef ENABLE_COAP
    { kMtpProtocol_CoAP, "CoAP" },
#endif
};

//------------------------------------------------------------------------------
// Table used to convert from an enumeration of an MTP status to a textual representation
const enum_entry_t mtp_statuses[] =
{
    { kMtpStatus_Error,  "Error" },
    { kMtpStatus_Down,   "Down" },
    { kMtpStatus_Up,     "Up" },
};

//------------------------------------------------------------------------------
// Forward declarations. Note these are not static, because we need them in the symbol table for USP_LOG_Callstack() to show them
int ValidateAdd_AgentMtp(dm_req_t *req);
int Notify_AgentMtpAdded(dm_req_t *req);
int Notify_AgentMtpDeleted(dm_req_t *req);
int Validate_AgentMtpProtocol(dm_req_t *req, char *value);
int Validate_AgentMtpStompReference(dm_req_t *req, char *value);
int Validate_AgentMtpStompDestination(dm_req_t *req, char *value);
int NotifyChange_AgentMtpEnable(dm_req_t *req, char *value);
int NotifyChange_AgentMtpProtocol(dm_req_t *req, char *value);
#ifdef ENABLE_COAP
int NotifyChange_AgentMtpCoAPPort(dm_req_t *req, char *value);
int NotifyChange_AgentMtpCoAPPath(dm_req_t *req, char *value);
#endif
int NotifyChange_AgentMtpStompReference(dm_req_t *req, char *value);
int NotifyChange_AgentMtpStompDestination(dm_req_t *req, char *value);
int Get_MtpStatus(dm_req_t *req, char *buf, int len);
int Get_StompDestFromServer(dm_req_t *req, char *buf, int len);
int ProcessAgentMtpAdded(int instance);
agent_mtp_t *FindUnusedAgentMtp(void);
void DestroyAgentMtp(agent_mtp_t *mtp);
agent_mtp_t *FindAgentMtpByInstance(int instance);

/*********************************************************************//**
**
** DEVICE_MTP_Init
**
** Initialises this component, and registers all parameters which it implements
**
** \param   None
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int DEVICE_MTP_Init(void)
{
    int err = USP_ERR_OK;
    int i;
    agent_mtp_t *mtp;

    // Mark all agent mtp slots as unused
    memset(agent_mtps, 0, sizeof(agent_mtps));
    for (i=0; i<MAX_AGENT_MTPS; i++)
    {
        mtp = &agent_mtps[i];
        mtp->instance = INVALID;
    }

    // Register parameters implemented by this component
    err |= USP_REGISTER_Object(DEVICE_AGENT_MTP_ROOT ".{i}", ValidateAdd_AgentMtp, NULL, Notify_AgentMtpAdded, 
                                                             NULL, NULL, Notify_AgentMtpDeleted);
    err |= USP_REGISTER_Param_NumEntries("Device.LocalAgent.MTPNumberOfEntries", DEVICE_AGENT_MTP_ROOT ".{i}");
    err |= USP_REGISTER_DBParam_Alias(DEVICE_AGENT_MTP_ROOT ".{i}.Alias", NULL); 

    err |= USP_REGISTER_DBParam_ReadWrite(DEVICE_AGENT_MTP_ROOT ".{i}.Protocol", "STOMP", Validate_AgentMtpProtocol, NotifyChange_AgentMtpProtocol, DM_STRING);
    err |= USP_REGISTER_DBParam_ReadWrite(DEVICE_AGENT_MTP_ROOT ".{i}.Enable", "false", NULL, NotifyChange_AgentMtpEnable, DM_BOOL);
    err |= USP_REGISTER_DBParam_ReadWrite(DEVICE_AGENT_MTP_ROOT ".{i}.STOMP.Reference", "", DEVICE_MTP_ValidateStompReference, NotifyChange_AgentMtpStompReference, DM_STRING);
    err |= USP_REGISTER_DBParam_ReadWrite(DEVICE_AGENT_MTP_ROOT ".{i}.STOMP.Destination", "", NULL, NotifyChange_AgentMtpStompDestination, DM_STRING);
    err |= USP_REGISTER_VendorParam_ReadOnly(DEVICE_AGENT_MTP_ROOT ".{i}.STOMP.DestinationFromServer", Get_StompDestFromServer, DM_STRING);
#ifdef ENABLE_COAP
    err |= USP_REGISTER_DBParam_ReadWrite(DEVICE_AGENT_MTP_ROOT ".{i}.CoAP.Port", "5683", DM_ACCESS_ValidatePort, NotifyChange_AgentMtpCoAPPort, DM_UINT);
    err |= USP_REGISTER_DBParam_ReadWrite(DEVICE_AGENT_MTP_ROOT ".{i}.CoAP.Path", "", NULL, NotifyChange_AgentMtpCoAPPath, DM_STRING);
#endif
    err |= USP_REGISTER_VendorParam_ReadOnly(DEVICE_AGENT_MTP_ROOT ".{i}.Status", Get_MtpStatus, DM_STRING);

    // Exit if any errors occurred
    if (err != USP_ERR_OK)
    {
        return USP_ERR_INTERNAL_ERROR;
    }

    // If the code gets here, then registration was successful
    return USP_ERR_OK;
}

/*********************************************************************//**
**
** DEVICE_MTP_Start
**
** Initialises the agent mtp array with the values of all agent MTPs from the DB
**
** \param   None
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int DEVICE_MTP_Start(void)
{
    int i;
    int_vector_t iv;
    int instance;
    int err;
    char path[MAX_DM_PATH];

    // Exit if unable to get the object instance numbers present in the agent MTP table
    err = DATA_MODEL_GetInstances(DEVICE_AGENT_MTP_ROOT, &iv);
    if (err != USP_ERR_OK)
    {
        return err;
    }

    // Issue a warning, if no local agent MTPs are present in database
    if (iv.num_entries == 0)
    {
        USP_LOG_Warning("%s: WARNING: No instances in %s. USP Agent can only be accessed via CLI.", __FUNCTION__, device_agent_mtp_root);
        err = USP_ERR_OK;
        goto exit;
    }

    // Add all agent MTPs to the agent mtp array
    for (i=0; i < iv.num_entries; i++)
    {
        instance = iv.vector[i];
        err = ProcessAgentMtpAdded(instance);
        if (err != USP_ERR_OK)
        {
            // Exit if unable to delete an agent MTP with bad parameters from the DB
            USP_SNPRINTF(path, sizeof(path), "%s.%d", device_agent_mtp_root, instance);
            USP_LOG_Warning("%s: Deleting %s as it contained invalid parameters.", __FUNCTION__, path);
            err = DATA_MODEL_DeleteInstance(path, 0);
            if (err != USP_ERR_OK)
            {
                goto exit;
            }
        }
    }

    err = USP_ERR_OK;

exit:
    // Destroy the vector of instance numbers for the table
    INT_VECTOR_Destroy(&iv);
    return err;
}

/*********************************************************************//**
**
** DEVICE_MTP_Stop
**
** Frees up all memory associated with this module
**
** \param   None
**
** \return  None
**
**************************************************************************/
void DEVICE_MTP_Stop(void)
{
    int i;
    agent_mtp_t *mtp;

    // Iterate over all agent MTPs, freeing all memory used by them
    for (i=0; i<MAX_AGENT_MTPS; i++)
    {
        mtp = &agent_mtps[i];
        if (mtp->instance != INVALID)
        {
            DestroyAgentMtp(mtp);
        }
    }
}


/******************************************************************//**
**
** DEVICE_MTP_GetAgentStompQueue
**
** Gets the name of the STOMP queue to use for this agent on a particular STOMP connection
**
** \param   instance - instance number of STOMP Connection in the Device.STOMP.Connection.{i} table
**
** \return  pointer to queue name, or NULL if unable to resolve the STOMP connection
**          NOTE: This may be NULL, if agent's STOMP queue is set by subscribe_dest: STOMP header
**
**************************************************************************/
char *DEVICE_MTP_GetAgentStompQueue(int instance)
{
    int i;
    agent_mtp_t *mtp;

    // Iterate over all agent MTPs, finding the first one that matches the specified STOMP connection
    // NOTE: Ideally we would have ensured that the agent_queue_name was unique for the stomp_connection_instance
    //       However it is hard to make this work in real life because when performing an ADD request, this code does
    //       not have visibility of the other parameters being performed in the add transaction, and hence cannot
    //       check the combination of agent_queue_name and stomp_connection_instance
    for (i=0; i<MAX_AGENT_MTPS; i++)
    {
        mtp = &agent_mtps[i];
        if ((mtp->instance != INVALID) && (mtp->enable == true) && 
            (mtp->stomp_connection_instance == instance) && (mtp->protocol == kMtpProtocol_STOMP) &&
            (mtp->stomp_agent_queue[0] != '\0'))
        {
            return mtp->stomp_agent_queue;
        }
    }

    // If the code gets here, then no match has been found
    return NULL;
}

/******************************************************************//**
**
** DEVICE_MTP_EnumToString
**
** Convenience function to convert an MTP enumeration to its equivalent string
**
** \param   protocol - enumerated value to convert
**
** \return  pointer to string
**
**************************************************************************/
char *DEVICE_MTP_EnumToString(mtp_protocol_t protocol)
{
    return TEXT_UTILS_EnumToString(protocol, mtp_protocols, NUM_ELEM(mtp_protocols));
}

/*********************************************************************//**
**
** DEVICE_MTP_ValidateStompReference
**
** Validates Device.LocalAgent.Controller.{i}.MTP.{i}.STOMP.Reference
** and       Device.LocalAgent.MTP.{i}.STOMP.Reference
** by checking that it refers to a valid reference in the Device.STOMP.Connection table
**
** \param   req - pointer to structure identifying the parameter
** \param   value - value that the controller would like to set the parameter to
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int DEVICE_MTP_ValidateStompReference(dm_req_t *req, char *value)
{
    int err;
    int stomp_connection_instance;

    // Exit if the STOMP Reference refers to nothing. This can occur if a STOMP connection being referred to is deleted.
    if (*value == '\0')
    {
        return USP_ERR_OK;
    }

    err = DM_ACCESS_ValidateReference(value, "Device.STOMP.Connection.{i}", &stomp_connection_instance);

    return err;
}

/*********************************************************************//**
**
** DEVICE_MTP_GetStompReference
**
** Gets the instance number in the STOMP connection table by dereferencing the specified path
** NOTE: If the path is invalid, or the instance does not exist, then INVALID is 
**       returned for the instance number, along with an error
**
** \param   path - path of parameter which contains the reference
** \param   stomp_connection_instance - pointer to variable in which to return the instance number in the STOMP connection table
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int DEVICE_MTP_GetStompReference(char *path, int *stomp_connection_instance)
{
    int err;
    char value[MAX_DM_PATH];

    // Set default return value
    *stomp_connection_instance = INVALID;

    // Exit if unable to get the reference to the entry in the STOMP connection table
    // NOTE: This will return the default of an empty string if not present in the DB
    err = DATA_MODEL_GetParameterValue(path, value, sizeof(value), 0);
    if (err != USP_ERR_OK)
    {
        return err;
    }

    // Exit if the reference has not been setup yet
    if (*value == '\0')
    {
        *stomp_connection_instance = INVALID;
        return USP_ERR_OK;
    }

    // Exit if unable to determine STOMP connection table reference
    err = DM_ACCESS_ValidateReference(value, "Device.STOMP.Connection.{i}", stomp_connection_instance);
    if (err != USP_ERR_OK)
    {
        return err;
    }

    return USP_ERR_OK;
}


/*********************************************************************//**
**
** DEVICE_MTP_NotifyStompConnDeleted
**
** Called when a STOMP connection is deleted
** This code unpicks all references to the STOMP connection existing in the LocalAgent MTP table
**
** \param   stomp_instance - instance in Device.STOMP.Connection which has been deleted
**
** \return  None
**
**************************************************************************/
void DEVICE_MTP_NotifyStompConnDeleted(int stomp_instance)
{
    int i;
    agent_mtp_t *mtp;
    char path[MAX_DM_PATH];

    // Iterate over all agent MTPs, clearing out all references to the deleted STOMP connection
    for (i=0; i<MAX_AGENT_MTPS; i++)
    {
        mtp = &agent_mtps[i];
        if ((mtp->instance != INVALID) && (mtp->protocol == kMtpProtocol_STOMP) && (mtp->stomp_connection_instance == stomp_instance))
        {
            USP_SNPRINTF(path, sizeof(path), "Device.LocalAgent.MTP.%d.STOMP.Reference", mtp->instance);
            DATA_MODEL_SetParameterValue(path, "", 0);
        }
    }
}

/*********************************************************************//**
**
** ValidateAdd_AgentMtp
**
** Function called to determine whether an MTP may be added to an agent
**
** \param   req - pointer to structure identifying the agent MTP
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int ValidateAdd_AgentMtp(dm_req_t *req)
{
    agent_mtp_t *mtp;
        
    // Exit if unable to find a free MTP slot
    mtp = FindUnusedAgentMtp();
    if (mtp == NULL)
    {
        return USP_ERR_RESOURCES_EXCEEDED;        
    }

    return USP_ERR_OK;
}

/*********************************************************************//**
**
** Notify_AgentMtpAdded
**
** Function called when an MTP has been added to Device.LocalAgent.MTP.{i}
**
** \param   req - pointer to structure identifying the controller
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int Notify_AgentMtpAdded(dm_req_t *req)
{
    int err;

    err = ProcessAgentMtpAdded(inst1);

    return err;
}

/*********************************************************************//**
**
** Notify_AgentMtpDeleted
**
** Function called when an MTP has been deleted from Device.LocalAgent.MTP.{i}
**
** \param   req - pointer to structure identifying the controller
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int Notify_AgentMtpDeleted(dm_req_t *req)
{
    agent_mtp_t *mtp;

    // Exit if unable to find Agent MTP in the array
    // NOTE: We might not find it if it was never added. This could occur if deleting from the DB at startup when we detected that the database params were invalid
    mtp = FindAgentMtpByInstance(inst1);
    if (mtp == NULL)
    {
        return USP_ERR_OK;
    }

    // Exit if this MTP is not currently enbled (nothing more to do)
    if (mtp->enable == false)
    {
        return USP_ERR_OK;
    }

    // If the code gets here, then we are deleting an enabled MTP, so first turn off the protocol being used
    switch(mtp->protocol)
    {
        case kMtpProtocol_STOMP:
            // Schedule a reconnect after the present response has been sent
            if (mtp->stomp_connection_instance != INVALID)
            {
                DEVICE_STOMP_ScheduleReconnect(mtp->stomp_connection_instance);
            }
            break;

#ifdef ENABLE_COAP
        case kMtpProtocol_CoAP:
            COAP_StopServer(mtp->instance);
            break;
#endif
        default:
            break;
    }

    // Delete the agent mtp from the array, if it has not already been deleted
    DestroyAgentMtp(mtp);

    return USP_ERR_OK;
}

/*********************************************************************//**
**
** Validate_AgentMtpProtocol
**
** Validates Device.LocalAgent.MTP.{i}.Protocol
** by checking that it matches the protocols we support
**
** \param   req - pointer to structure identifying the parameter
** \param   value - value that the controller would like to set the parameter to
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int Validate_AgentMtpProtocol(dm_req_t *req, char *value)
{
    mtp_protocol_t protocol;

    // Exit if the protocol was invalid
    protocol = TEXT_UTILS_StringToEnum(value, mtp_protocols, NUM_ELEM(mtp_protocols));
    if (protocol == INVALID)
    {
        USP_ERR_SetMessage("%s: Invalid protocol %s", __FUNCTION__, value);
        return USP_ERR_INVALID_VALUE;
    }

    return USP_ERR_OK;
}

/*********************************************************************//**
**
** NotifyChange_AgentMtpEnable
**
** Function called when Device.LocalAgent.MTP.{i}.Enable is modified
** This function updates the value of the enable stored in the agent_mtp array
**
** \param   req - pointer to structure identifying the path
** \param   value - new value of this parameter
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int NotifyChange_AgentMtpEnable(dm_req_t *req, char *value)
{
    agent_mtp_t *mtp;

    // Determine MTP to be updated
    mtp = FindAgentMtpByInstance(inst1);
    USP_ASSERT(mtp != NULL);

    // Exit if the value has not changed
    if (val_bool == mtp->enable)
    {
        return USP_ERR_OK;
    }

    // Store the new value
    mtp->enable = val_bool;

    // Update the protocol based on the change
    switch(mtp->protocol)
    {
        case kMtpProtocol_STOMP:
            // Always schedule a reconnect for the affected STOMP connection instance
            // If this MTP has been disabled, then the reconnect will fail unless another MTP specifies the agent queue to subscribe to
            if (mtp->stomp_connection_instance != INVALID)
            {
                DEVICE_STOMP_ScheduleReconnect(mtp->stomp_connection_instance);
            }
            break;
 
#ifdef ENABLE_COAP
        case kMtpProtocol_CoAP:
            // Enable or disable the CoAP server based on the new value
            if (mtp->enable)
            {
                int err;
                err = COAP_StartServer(mtp->instance, AF_INET, "0.0.0.0", mtp->coap_port, mtp->coap_path);
                if (err != USP_ERR_OK)
                {
                    return err;
                }
            }
            else
            {
                COAP_StopServer(mtp->instance);
            }
            break;
 #endif
 
        default:
            break;
    }

    return USP_ERR_OK;
}

/*********************************************************************//**
**
** NotifyChange_AgentMtpProtocol
**
** Function called when Device.LocalAgent.MTP.{i}.Protocol is modified
**
** \param   req - pointer to structure identifying the path
** \param   value - new value of this parameter
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int NotifyChange_AgentMtpProtocol(dm_req_t *req, char *value)
{
    agent_mtp_t *mtp;
    mtp_protocol_t old_protocol;    

    // Determine MTP to be updated
    mtp = FindAgentMtpByInstance(inst1);
    USP_ASSERT(mtp != NULL);

    // Set the new value
    old_protocol = mtp->protocol;
    mtp->protocol = TEXT_UTILS_StringToEnum(value, mtp_protocols, NUM_ELEM(mtp_protocols));
    USP_ASSERT(mtp->protocol != INVALID);    // The enumeration has already been validated

    // Exit if this MTP is not enabled, nothing more to do
    if (mtp->enable == false)
    {
        return USP_ERR_OK;
    }

    // Exit if the value has not changed
    if (mtp->protocol == old_protocol)
    {
        return USP_ERR_OK;
    }

    // If the code gets here, then the protocol has changed from STOMP to CoAP, or vice versa
    // So schedule the affected STOMP connection to reconnect (because it might have lost or gained a agent queue to subscribe to)
    if ((mtp->enable) && (mtp->stomp_connection_instance != INVALID))
    {
        DEVICE_STOMP_ScheduleReconnect(mtp->stomp_connection_instance);
    }

#ifdef ENABLE_COAP
    // If the last protocol was CoAP, stop it's server
    if ((old_protocol == kMtpProtocol_CoAP) && (mtp->enable))
    {
        COAP_StopServer(mtp->instance);
    }

    // If the new protocol is CoAP, start it's server
    if ((mtp->protocol == kMtpProtocol_CoAP) && (mtp->enable))
    {
        int err;
        err = COAP_StartServer(mtp->instance, AF_INET, "0.0.0.0", mtp->coap_port, mtp->coap_path);
        if (err != USP_ERR_OK)
        {
            return err;
        }
    }
#endif

    return USP_ERR_OK;
}

#ifdef ENABLE_COAP
/*********************************************************************//**
**
** NotifyChange_AgentMtpCoAPPort
**
** Function called when Device.LocalAgent.MTP.{i}.CoAP.Port is modified
**
** \param   req - pointer to structure identifying the path
** \param   value - new value of this parameter
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int NotifyChange_AgentMtpCoAPPort(dm_req_t *req, char *value)
{
    int err;
    agent_mtp_t *mtp;

    // Find the mtp server associated with this change
    mtp = FindAgentMtpByInstance(inst1);
    USP_ASSERT(mtp != NULL);

    // Exit if port has not changed
    if (val_uint == mtp->coap_port)
    {
        return USP_ERR_OK;
    }

    // Store the new port
    mtp->coap_port = val_uint;

    // Restart the CoAP server, if enabled
    if ((mtp->protocol == kMtpProtocol_CoAP) && (mtp->enable))
    {
        COAP_StopServer(inst1);
        err = COAP_StartServer(mtp->instance, AF_INET, "0.0.0.0", mtp->coap_port, mtp->coap_path);
        if (err != USP_ERR_OK)
        {
            return err;
        }
    }

    return USP_ERR_OK;
}

/*********************************************************************//**
**
** NotifyChange_AgentMtpCoAPPath
**
** Function called when Device.LocalAgent.MTP.{i}.CoAP.Path is modified
**
** \param   req - pointer to structure identifying the path
** \param   value - new value of this parameter
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int NotifyChange_AgentMtpCoAPPath(dm_req_t *req, char *value)
{
    int err;
    agent_mtp_t *mtp;

    // Find the mtp server associated with this change
    mtp = FindAgentMtpByInstance(inst1);
    USP_ASSERT(mtp != NULL);

    // Propagate the changed path
    USP_SAFE_FREE(mtp->coap_path);
    mtp->coap_path = USP_STRDUP(value);

    // Restart the CoAP server, if enabled
    if ((mtp->protocol == kMtpProtocol_CoAP) && (mtp->enable))
    {
        COAP_StopServer(inst1);
        err = COAP_StartServer(mtp->instance, AF_INET, "0.0.0.0", mtp->coap_port, mtp->coap_path);
        if (err != USP_ERR_OK)
        {
            return err;
        }
    }

    return USP_ERR_OK;
}
#endif // ENABLE_COAP

/*********************************************************************//**
**
** NotifyChange_AgentMtpStompReference
**
** Function called when Device.LocalAgent.MTP.{i}.STOMP.Reference is modified
**
** \param   req - pointer to structure identifying the path
** \param   value - new value of this parameter
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int NotifyChange_AgentMtpStompReference(dm_req_t *req, char *value)
{
    int err;
    agent_mtp_t *mtp;
    char path[MAX_DM_PATH];
    int last_connection_instance;
    int new_connection_instance;

    // Determine MTP to be updated
    mtp = FindAgentMtpByInstance(inst1);
    USP_ASSERT(mtp != NULL);

    // Exit if unable to extract the new value
    new_connection_instance = INVALID;
    USP_SNPRINTF(path, sizeof(path), "%s.%d.STOMP.Reference", device_agent_mtp_root, inst1);
    err = DEVICE_MTP_GetStompReference(path, &new_connection_instance);
    if (err != USP_ERR_OK)
    {
        mtp->stomp_connection_instance = INVALID;
        return err;
    }

    // Set the new value. This is done before scheduling a reconnect so that the reconnect uses these parameters
    last_connection_instance = mtp->stomp_connection_instance;
    mtp->stomp_connection_instance = new_connection_instance;

    // Schedule a reconnect after the present response has been sent, if the value has changed
    if ((mtp->enable == true) && (mtp->protocol == kMtpProtocol_STOMP) &&
        (last_connection_instance != new_connection_instance))
    {
        if (last_connection_instance != INVALID)
        {
            DEVICE_STOMP_ScheduleReconnect(last_connection_instance);
        }

        if (new_connection_instance != INVALID)
        {
            DEVICE_STOMP_ScheduleReconnect(new_connection_instance);
        }
    }

    return err;
}

/*********************************************************************//**
**
** NotifyChange_AgentMtpStompDestination
**
** Function called when Device.LocalAgent.MTP.{i}.STOMP.Destination is modified
**
** \param   req - pointer to structure identifying the path
** \param   value - new value of this parameter
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int NotifyChange_AgentMtpStompDestination(dm_req_t *req, char *value)
{
    agent_mtp_t *mtp;
    bool schedule_reconnect = false;

    // Determine MTP to be updated
    mtp = FindAgentMtpByInstance(inst1);
    USP_ASSERT(mtp != NULL);

    // Determine whether to reconnect
    if ((mtp->enable == true) && (mtp->protocol == kMtpProtocol_STOMP) &&
        (strcmp(mtp->stomp_agent_queue, value) != 0))
    {
        if (mtp->stomp_connection_instance != INVALID)
        {
            schedule_reconnect = true;
        }
    }

    // Set the new value
    // This is done before scheduling a reconnect, so that the reconnect is done with the new parameters
    USP_SAFE_FREE(mtp->stomp_agent_queue);
    mtp->stomp_agent_queue = USP_STRDUP(value);

    if (schedule_reconnect)
    {
        DEVICE_STOMP_ScheduleReconnect(mtp->stomp_connection_instance);
    }
    
    return USP_ERR_OK;
}

/*********************************************************************//**
**
** Get_MtpStatus
**
** Function called to get the value of Device.LocalAgent.MTP.{i}.Status
**
** \param   req - pointer to structure identifying the path
** \param   buf - pointer to buffer in which to return the value
** \param   len - length of buffer
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int Get_MtpStatus(dm_req_t *req, char *buf, int len)
{
    agent_mtp_t *mtp;
    mtp_status_t status;
    char *str;

    // Determine which MTP to get the status of
    mtp = FindAgentMtpByInstance(inst1);
    USP_ASSERT(mtp != NULL);

    // Get the status, based on the protocol
    if (mtp->enable)
    {
        switch(mtp->protocol)
        {
            case kMtpProtocol_STOMP:
                status = DEVICE_STOMP_GetMtpStatus(mtp->stomp_connection_instance);
                break;
     
#ifdef ENABLE_COAP
            case kMtpProtocol_CoAP:
                status = COAP_GetServerStatus(mtp->instance);
                break;
#endif

            default:
                // NOTE: The code should never get here, as we only allow valid MTPs to be set
                status = kMtpStatus_Error;
                break;
        }
    }
    else
    {
        // If not enabled, then always report that the interface is down
        status = kMtpStatus_Down;
    }

    // Convert to a string representation and copy into return buffer
    str = TEXT_UTILS_EnumToString(status, mtp_statuses, NUM_ELEM(mtp_statuses));
    USP_STRNCPY(buf, str, len);

    return USP_ERR_OK;
}

/*********************************************************************//**
**
** Get_StompDestFromServer
**
** Function called to get the value of Device.LocalAgent.MTP.{i}.DestinationFromServer
**
** \param   req - pointer to structure identifying the path
** \param   buf - pointer to buffer in which to return the value
** \param   len - length of buffer
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int Get_StompDestFromServer(dm_req_t *req, char *buf, int len)
{
    agent_mtp_t *mtp;

    // Set the default return value
    *buf = '\0';

    // Determine which MTP to query
    mtp = FindAgentMtpByInstance(inst1);
    USP_ASSERT(mtp != NULL);

    // Get the DestinationFromServer
    if ((mtp->enable) && (mtp->protocol==kMtpProtocol_STOMP))
    {
        DEVICE_STOMP_GetDestinationFromServer(mtp->stomp_connection_instance, buf, len);
    }

    return USP_ERR_OK;
}

/*********************************************************************//**
**
** ProcessAgentMtpAdded
**
** Reads the parameters for the specified MTP from the database and processes them
**
** \param   instance - instance number of the MTP in the local agent MTP table
**
** \return  USP_ERR_OK if successful
**
**************************************************************************/
int ProcessAgentMtpAdded(int instance)
{
    agent_mtp_t *mtp;
    int err;
    char path[MAX_DM_PATH];

    // Exit if unable to add another agent MTP
    mtp = FindUnusedAgentMtp();
    if (mtp == NULL)
    {
        return USP_ERR_RESOURCES_EXCEEDED;        
    }

    // Initialise to defaults
    memset(mtp, 0, sizeof(agent_mtp_t));
    mtp->instance = instance;
    mtp->stomp_connection_instance = INVALID;
    mtp->instance = instance;

    // Exit if unable to determine whether this agent MTP was enabled or not
    USP_SNPRINTF(path, sizeof(path), "%s.%d.Enable", device_agent_mtp_root, instance);
    err = DM_ACCESS_GetBool(path, &mtp->enable);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }

    // Exit if unable to get the protocol for this MTP
    USP_SNPRINTF(path, sizeof(path), "%s.%d.Protocol", device_agent_mtp_root, instance);
    err = DM_ACCESS_GetEnum(path, &mtp->protocol, mtp_protocols, NUM_ELEM(mtp_protocols));
    if (err != USP_ERR_OK)
    {
        goto exit;
    }

    // NOTE: We attempt to get all parameters, irrespective of the protocol that was actually set
    // This is because the data model allows us to setup both STOMP and CoAP params at the same time, and just select between them using the protocol parameter
    // If the parameter is not present in the database, then it's default value will be read

    // Exit if there was an error in the reference to the entry in the STOMP connection table
    USP_SNPRINTF(path, sizeof(path), "%s.%d.STOMP.Reference", device_agent_mtp_root, instance);
    err = DEVICE_MTP_GetStompReference(path, &mtp->stomp_connection_instance);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }
    
    // Exit if unable to get the name of the agent's STOMP queue
    USP_SNPRINTF(path, sizeof(path), "%s.%d.STOMP.Destination", device_agent_mtp_root, instance);
    err = DM_ACCESS_GetString(path, &mtp->stomp_agent_queue);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }

#ifdef ENABLE_COAP
    // Exit if unable to get the listening port to use for CoAP
    USP_SNPRINTF(path, sizeof(path), "%s.%d.CoAP.Port", device_agent_mtp_root, instance);
    err = DM_ACCESS_GetUnsigned(path, &mtp->coap_port);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }

    // Exit if unable to get the name of the agent's CoAP resource name path
    USP_SNPRINTF(path, sizeof(path), "%s.%d.CoAP.Path", device_agent_mtp_root, instance);
    err = DM_ACCESS_GetString(path, &mtp->coap_path);
    if (err != USP_ERR_OK)
    {
        goto exit;
    }

    // Exit if the protocol was CoAP and unable to start a CoAP server
    if ((mtp->protocol == kMtpProtocol_CoAP) && (mtp->enable))
    {
        err = COAP_StartServer(mtp->instance, AF_INET, "0.0.0.0", mtp->coap_port, mtp->coap_path);
        if (err != USP_ERR_OK)
        {
            goto exit;
        }
    }
#endif

    // If the code gets here, then we successfully retrieved all data about the MTP
    err = USP_ERR_OK;

exit:
    if (err != USP_ERR_OK)
    {
        DestroyAgentMtp(mtp);
    }

    // Schedule a STOMP reconnect, if this MTP affects an existing STOMP connection
    if ((mtp->enable) && (mtp->protocol==kMtpProtocol_STOMP) && (mtp->stomp_connection_instance != INVALID))
    {
        DEVICE_STOMP_ScheduleReconnect(mtp->stomp_connection_instance);
    }

    return err;
}

/*********************************************************************//**
**
** FindUnusedAgentMtp
**
** Finds the first free agent MTP slot
**
** \param   None
**
** \return  Pointer to first free agent MTP, or NULL if no free agent MTP slot found
**
**************************************************************************/
agent_mtp_t *FindUnusedAgentMtp(void)
{
    int i;
    agent_mtp_t *mtp;

    // Iterate over all agent MTPs
    for (i=0; i<MAX_AGENT_MTPS; i++)
    {
        // Exit if found an unused slot
        mtp = &agent_mtps[i];
        if (mtp->instance == INVALID)
        {
            return mtp;
        }
    }

    // If the code gets here, then no free slot has been found
    USP_ERR_SetMessage("%s: Only %d agent MTPs are supported.", __FUNCTION__, MAX_AGENT_MTPS);
    return NULL;
}

/*********************************************************************//**
**
** DestroyAgentMtp
**
** Frees all memory associated with the specified agent mtp slot
**
** \param   cont - pointer to controller to free
**
** \return  None
**
**************************************************************************/
void DestroyAgentMtp(agent_mtp_t *mtp)
{
    mtp->instance = INVALID;      // Mark agent MTP slot as free
    mtp->protocol = kMtpProtocol_None;
    mtp->enable = false;
    mtp->stomp_connection_instance = INVALID;

    USP_SAFE_FREE(mtp->stomp_agent_queue);
#ifdef ENABLE_COAP
    USP_SAFE_FREE(mtp->coap_path);
    mtp->coap_port = 0;
#endif
    
}

/*********************************************************************//**
**
** FindAgentMtpByInstance
**
** Finds an agent MTP entry by it's data model instance number
**
** \param   instance - instance number of the agent MTP in the data model
**
** \return  pointer to entry within the agent_mtps array, or NULL if mtp was not found
**
**************************************************************************/
agent_mtp_t *FindAgentMtpByInstance(int instance)
{
    int i;
    agent_mtp_t *mtp;

    // Iterate over all agent MTPs
    for (i=0; i<MAX_AGENT_MTPS; i++)
    {
        // Exit if found an agent mtp that matches the instance number
        mtp = &agent_mtps[i];
        if (mtp->instance == instance)
        {
            return mtp;
        }
    }

    // If the code gets here, then no matching MTP was found
    return NULL;
}

