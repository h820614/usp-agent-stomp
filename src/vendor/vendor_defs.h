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
 * \file vendor_defs.h
 *
 * Header file containing defines controlling the build of USP Agent,
 * which may be modified by the vendor
 *
 */
#ifndef VENDOR_DEFS_H
#define VENDOR_DEFS_H

//------------------------------------------------------------------------------
// Definitions used to size static arrays
// You are unlikely to need to change these
#define MAX_DM_INSTANCES 128      // Maximum number of instances of a single object
#define MAX_DM_INSTANCE_ORDER 6   // Maximum number of instance numbers in a data model schema path (ie number of '{i}' in the schema path)
#define MAX_DM_PATH (256)           // Maximum number of characters in a data model path
#define MAX_DM_VALUE_LEN (4096)     // Maximum number of characters in a data model parameter value
#define MAX_DM_SHORT_VALUE_LEN (MAX_DM_PATH) // Maximum number of characters in an (expected to be) short data model parameter value
#define MAX_PATH_SEGMENTS (32)      // Maximum number of segments (eg "Device, "LocalAgent") in a path. Does not include instance numbers.
#define MAX_COMPOUND_KEY_PARAMS 4   // Maximum number of parameters in a compound unique key
#define MAX_CONTROLLERS 5           // Maximum number of controllers which may be present in the DB (Device.LocalAgent.Controller.{i})
#define MAX_CONTROLLER_MTPS 3       // Maximum number of MTPs that a controller may have in the DB (Device.LocalAgent.Controller.{i}.MTP.{i})
#define MAX_AGENT_MTPS (MAX_CONTROLLERS)  // Maximum number of MTPs that an agent may have in the DB (Device.LocalAgent.MTP.{i})
#define MAX_STOMP_CONNECTIONS (MAX_CONTROLLERS)  // Maximum number of STOMP connections that an agent may have in the DB (Device.STOMP.Connection.{i})
#define MAX_COAP_CONNECTIONS (MAX_CONTROLLERS)  // Maximum number of CoAP connections that an agent may have in the DB (Device.LocalAgent.Controller.{i}.MTP.{i}.CoAP)
#define MAX_COAP_SERVERS 2          // Maximum number of interfaces which an agent listens for CoAP messages on
#define MAX_FIRMWARE_IMAGES 2       // Maximum number of firmware images that the CPE can hold in flash at any one time
#define MAX_ACTIVATE_TIME_WINDOWS 5 // Maximum number of time windows allowed in the Activate() command's input arguments

// Maximum number of bytes allowed in a USP protobuf message. 
// This is not used to size any arrays, just used as a security measure to prevent rogue controllers crashing 
// the agent process with out of memory
#define MAX_USP_MSG_LEN (64*1024)

// Period of time (in seconds) between polling values that have value change notification enabled on them
#define VALUE_CHANGE_POLL_PERIOD  (30)

// Location of the database file to use, if none is specified on the command line when invoking this executable
// NOTE: As the database needs to be stored persistently, this should be changed to a directory which is not cleared on boot up
#define DEFAULT_DATABASE_FILE               "/tmp/usp.db"

// Location of unix domain stream file used for CLI communication between client and server
#define CLI_UNIX_DOMAIN_FILE                "/tmp/usp_cli"

//-----------------------------------------------------------------------------------------
// Defines associated with factory reset database
// Location of the file containing a factory reset database (SQLite database file)
// NOTE: This may be NULL or an empty string, if the factory reset database is created by an external script, rather than being a simple fixed file.
#define FACTORY_RESET_FILE      ""

// Uncomment the following to get the values of the factory reset parameters from VENDOR_GetFactoryResetParams()
#define INCLUDE_PROGRAMMATIC_FACTORY_RESET

//-----------------------------------------------------------------------------------------
// Uncomment the following to remove code and features from the standard build
//#define REMOVE_DEVICE_INFO               // Removes DeviceInfo from the core data model. It must instead be provided by the vendor.
//#define REMOVE_SELF_TEST_DIAG_EXAMPLE    // Removes Self Test diagnostics example code

//#define DONT_SORT_GET_INSTANCES          // Disables the sorting of data model paths returned in a GetInstancesResponse. Useful for slow devices supporting large data models.

// Uncomment the following defines to add code and features to the standard build
//#define VALIDATE_OUTPUT_ARG_NAMES        // Checks that the output argument names in operations and events formed by code in USP Agent 
                                           // match the schema registered in the data model by USP_REGISTER_OperationArguments() and USP_REGISTER_EventArguments
//-----------------------------------------------------------------------------------------
// The following define controls whether STOMP connects over the default WAN interface, or
// whether the Linux routing tables can decide which interface to use
// Comment out the following define if you want to let the Linux routing tables decide which network interface to use for USP connections
// Letting the Linux routing tables decide is better for devices that can connect to the STOMP server through either
// WiFi or ethernet, and either of these interfaces could be down at any one time
#define CONNECT_ONLY_OVER_WAN_INTERFACE

//-----------------------------------------------------------------------------------------
// OUI (Organization Unique Identifier) to use for this CPE. This code will be unique to the manufacturer
// This may be overridden by an environment variable. See GetDefaultOUI(). Or by a vendor hook for Device.DeviceInfo.ManufacturerOUI (if REMOVE_DEVICE_INFO is defined)
#define VENDOR_OUI "012345"

// Various defines for constant parameters in Device.DeviceInfo
// These defines are only used if USP Agent core implements DeviceInfo (see REMOVE_DEVICE_INFO above)
// These defines MUST be modified by the vendor
#define VENDOR_PRODUCT_CLASS "USP Agent"   // Configures the value of Device.DeviceInfo.ProductClass
#define VENDOR_MANUFACTURER  "Manufacturer"       // Configures the value of Device.DeviceInfo.Manufacturer
#define VENDOR_MODEL_NAME    "USP Agent"   // Configures the value of Device.DeviceInfo.ModelName

// URI of data model implemented by USP Agent
#define BBF_DATA_MODEL_URI "urn:broadband-forum-org:tr-181-2-12-0"

// Name of interface on which the WAN is connected.
// This interface is used to get the serial number of the agent (as MAC address) for the endpoint_id string
// It is also the interface used for all USP communications
// This may be overridden by an environment variable. See nu_macaddr_wan_ifname().
#define DEFAULT_WAN_IFNAME "eth0"

// Key used to obfuscate (using XOR) all secure data model parameters stored in the USP Agent database (eg passwords)
#define PASSWORD_OBFUSCATION_KEY  "$%^&*()@~#/,?"

// Timeout (in seconds) when performing a connect to a STOMP broker
#define STOMP_CONNECT_TIMEOUT 30

// Delay before starting USP Agent as a daemon. Used as a workaround in cases where other services (eg DNS) are not ready at the time USP Agent is started
#define DAEMON_START_DELAY_MS   0

//-----------------------------------------------------------------------------------------
// Defines for Bulk Data Collection
// NOTE: Some of these integer values are converted to string literals by C-preprocessor for registering parameter defaults
//       So these values must be simple ints, and must not contain brackets etc
//       If after modifying, you are unsure, try reading back the default values from an empty database and checking that they make sense
#define BULKDATA_MAX_PROFILES 5                    // Maximum number of bulk data profiles supported
#define BULKDATA_MAX_RETAINED_FAILED_REPORTS 3     // Maximum number of retained failed bulk data reports
#define BULKDATA_MINIMUM_REPORTING_INTERVAL 300    // Minimum supported reporting interval, in seconds
#define BULKDATA_HTTP_AUTH_METHOD  CURLAUTH_BASIC  // HTTP Authentication method to use. Note: Normally over https

#define BULKDATA_CONNECT_TIMEOUT 30   // Timeout (in seconds) when attempting to connect to a bulk data collection server
#define BULKDATA_TOTAL_TIMEOUT   60   // Total timeout (in seconds) to connect and send to a bulk data collection server
                                      // BULKDATA_TOTAL_TIMEOUT includes BULKDATA_CONNECT_TIMEOUT, so should be larger than it.

//-----------------------------------------------------------------------------------------
// Static Declaration of all Controller Trust roles
// The names of all enumerations may be altered, and enumerations added/deleted, but the last entry must always be kCTrustRole_Max
typedef enum
{
    kCTrustRole_FullAccess,
    kCTrustRole_Untrusted,

    kCTrustRole_Max         // This must always be the last entry in this enumeration. It is used to statically size arrays
} ctrust_role_t;

// Definitions of roles used
#define ROLE_NON_SSL       kCTrustRole_FullAccess  // Role to use, if SSL is not being used
#define ROLE_DEFAULT       kCTrustRole_FullAccess  // Default Role to use for controllers until determined from MTP certificate
#define ROLE_COAP          kCTrustRole_FullAccess  // Role to use for all CoAP communications





#endif

