/**
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *
 *    Copyright (c) 2020 Silicon Labs
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
/***************************************************************************/
/**
 * @file
 * @brief Contains the per-endpoint configuration of
 *attribute tables.
 *******************************************************************************
 ******************************************************************************/

#pragma once

//#include PLATFORM_HEADER
#include <app/AttributeAccessInterface.h>
#include <app/ConcreteAttributePath.h>
#include <app/util/af.h>
#include <platform/CHIPDeviceLayer.h>

#if !defined(EMBER_SCRIPTED_TEST)
#include <app-common/zap-generated/att-storage.h>
#endif

#if !defined(ATTRIBUTE_STORAGE_CONFIGURATION) && defined(EMBER_TEST)
#define ATTRIBUTE_STORAGE_CONFIGURATION "attribute-storage-test.h"
#endif

// ATTRIBUTE_STORAGE_CONFIGURATION macro
// contains the file that contains the initial set-up of the
// attribute data structures. If it is missing
// we use the provider sample.
#ifndef ATTRIBUTE_STORAGE_CONFIGURATION
//  #error "Must define ATTRIBUTE_STORAGE_CONFIGURATION to specify the App. Builder default attributes file."
#include <zap-generated/endpoint_config.h>
#else
#include ATTRIBUTE_STORAGE_CONFIGURATION
#endif

// If we have fixed number of endpoints, then max is the same.
#ifdef FIXED_ENDPOINT_COUNT
#define MAX_ENDPOINT_COUNT (FIXED_ENDPOINT_COUNT + CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
#endif

#include <app-common/zap-generated/attribute-type.h>

#define DECLARE_DYNAMIC_ENDPOINT(endpointName, clusterList)                                                                        \
    EmberAfEndpointType endpointName = { clusterList, sizeof(clusterList) / sizeof(EmberAfCluster), 0 }

#define DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(clusterListName) EmberAfCluster clusterListName[] = {

#define DECLARE_DYNAMIC_CLUSTER(clusterId, clusterAttrs)                                                                           \
    {                                                                                                                              \
        clusterId, clusterAttrs, sizeof(clusterAttrs) / sizeof(EmberAfAttributeMetadata), 0, ZAP_CLUSTER_MASK(SERVER), NULL        \
    }

#define DECLARE_DYNAMIC_CLUSTER_LIST_END }

#define DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(attrListName) EmberAfAttributeMetadata attrListName[] = {

#define DECLARE_DYNAMIC_ATTRIBUTE_LIST_END()                                                                                       \
    {                                                                                                                              \
        0xFFFD, ZAP_TYPE(INT16U), 2, ZAP_ATTRIBUTE_MASK(EXTERNAL_STORAGE), ZAP_EMPTY_DEFAULT()                                     \
    } /* cluster revision */                                                                                                       \
    }

#define DECLARE_DYNAMIC_ATTRIBUTE(attId, attType, attSizeBytes, attrMask)                                                          \
    {                                                                                                                              \
        attId, ZAP_TYPE(attType), attSizeBytes, attrMask | ZAP_ATTRIBUTE_MASK(EXTERNAL_STORAGE), ZAP_EMPTY_DEFAULT()               \
    }

#define CLUSTER_TICK_FREQ_ALL (0x00)
#define CLUSTER_TICK_FREQ_QUARTER_SECOND (0x04)
#define CLUSTER_TICK_FREQ_HALF_SECOND (0x08)
#define CLUSTER_TICK_FREQ_SECOND (0x0C)

extern uint8_t attributeData[]; // main storage bucket for all attributes

extern uint8_t attributeDefaults[]; // storage bucked for > 2b default values

void emAfCallInits(void);

#define emberAfClusterIsClient(cluster) ((bool) (((cluster)->mask & CLUSTER_MASK_CLIENT) != 0))
#define emberAfClusterIsServer(cluster) ((bool) (((cluster)->mask & CLUSTER_MASK_SERVER) != 0))
#define emberAfDoesClusterHaveInitFunction(cluster) ((bool) (((cluster)->mask & CLUSTER_MASK_INIT_FUNCTION) != 0))
#define emberAfDoesClusterHaveAttributeChangedFunction(cluster)                                                                    \
    ((bool) (((cluster)->mask & CLUSTER_MASK_ATTRIBUTE_CHANGED_FUNCTION) != 0))
#define emberAfDoesClusterHaveDefaultResponseFunction(cluster)                                                                     \
    ((bool) (((cluster)->mask & CLUSTER_MASK_DEFAULT_RESPONSE_FUNCTION) != 0))
#define emberAfDoesClusterHaveMessageSentFunction(cluster) ((bool) (((cluster)->mask & CLUSTER_MASK_MESSAGE_SENT_FUNCTION) != 0))

// Initial configuration
void emberAfEndpointConfigure(void);

EmberAfStatus emAfReadOrWriteAttribute(EmberAfAttributeSearchRecord * attRecord, EmberAfAttributeMetadata ** metadata,
                                       uint8_t * buffer, uint16_t readLength, bool write);

bool emAfMatchCluster(EmberAfCluster * cluster, EmberAfAttributeSearchRecord * attRecord);
bool emAfMatchAttribute(EmberAfCluster * cluster, EmberAfAttributeMetadata * am, EmberAfAttributeSearchRecord * attRecord);

// Check if a cluster is implemented or not. If yes, the cluster is returned.
//
// mask = 0 -> find either client or server
// mask = CLUSTER_MASK_CLIENT -> find client
// mask = CLUSTER_MASK_SERVER -> find server
//
// If a pointer to an index is provided, it will be updated to point to the relative index of the cluster
// within the set of clusters that match the mask criteria.
//
EmberAfCluster * emberAfFindClusterInType(EmberAfEndpointType * endpointType, chip::ClusterId clusterId, EmberAfClusterMask mask,
                                          uint8_t * index = nullptr);

// For a given cluster and mask, retrieves the list of endpoints sorted by endpoint that contain the matching cluster and returns
// the index within that list that matches the given endpoint.
//
// Mask is either CLUSTER_MASK_CLIENT or CLUSTER_MASK_SERVER
// For example, if you have 3 endpoints, 10, 11, 12, and cluster X server is
// located on 11 and 12, and cluster Y server is located only on 10 then
//    clusterIndex(X,11,CLUSTER_MASK_SERVER) returns 0,
//    clusterIndex(X,12,CLUSTER_MASK_SERVER) returns 1,
//    clusterIndex(X,10,CLUSTER_MASK_SERVER) returns 0xFF
//    clusterIndex(Y,10,CLUSTER_MASK_SERVER) returns 0
//    clusterIndex(Y,11,CLUSTER_MASK_SERVER) returns 0xFF
//    clusterIndex(Y,12,CLUSTER_MASK_SERVER) returns 0xFF
uint8_t emberAfClusterIndexInMatchingEndpoints(chip::EndpointId endpoint, chip::ClusterId clusterId, EmberAfClusterMask mask);

//
// Given a cluster ID, endpoint ID and a cluster mask, finds a matching cluster within that endpoint
// with a matching mask. If one is found, the relative index of that cluster within the list of clusters on that
// endpoint is returned. Otherwise, 0xFF is returned.
//
uint8_t emberAfClusterIndex(chip::EndpointId endpoint, chip::ClusterId clusterId, EmberAfClusterMask mask);

// If server == true, returns the number of server clusters,
// otherwise number of client clusters on this endpoint
uint8_t emberAfClusterCount(chip::EndpointId endpoint, bool server);

// Returns the cluster of Nth server or client cluster,
// depending on server toggle.
EmberAfCluster * emberAfGetNthCluster(chip::EndpointId endpoint, uint8_t n, bool server);

// Returns the clusterId of Nth server or client cluster,
// depending on server toggle.
// Returns Optional<ClusterId>::Missing if cluster does not exist.
chip::Optional<chip::ClusterId> emberAfGetNthClusterId(chip::EndpointId endpoint, uint8_t n, bool server);

// Returns number of clusters put into the passed cluster list
// for the given endpoint and client/server polarity
uint8_t emberAfGetClustersFromEndpoint(chip::EndpointId endpoint, chip::ClusterId * clusterList, uint8_t listLen, bool server);

// Returns cluster within the endpoint, or NULL if it isn't there
EmberAfCluster * emberAfFindCluster(chip::EndpointId endpoint, chip::ClusterId clusterId, EmberAfClusterMask mask);

// Returns cluster within the endpoint; Does not ignore disabled endpoints
EmberAfCluster * emberAfFindClusterIncludingDisabledEndpoints(chip::EndpointId endpoint, chip::ClusterId clusterId,
                                                              EmberAfClusterMask mask);

// Function mask must contain one of the CLUSTER_MASK function macros,
// then this method either returns the function pointer or null if
// function doesn't exist. Before you call the function, you must
// cast it.
EmberAfGenericClusterFunction emberAfFindClusterFunction(EmberAfCluster * cluster, EmberAfClusterMask functionMask);

// Public APIs for loading attributes
void emberAfInitializeAttributes(chip::EndpointId endpoint);
void emberAfResetAttributes(chip::EndpointId endpoint);

// Loads the attributes from built-in default and / or storage.  If
// ignoreStorage is true, only defaults will be read, and the storage for
// non-volatile attributes will be overwritten with those defaults.
void emAfLoadAttributeDefaults(chip::EndpointId endpoint, bool ignoreStorage);

// After the RAM value has changed, code should call this function. If this
// attribute has been tagged as non-volatile, its value will be stored.
void emAfSaveAttributeToStorageIfNeeded(uint8_t * data, chip::EndpointId endpoint, chip::ClusterId clusterId,
                                        EmberAfAttributeMetadata * metadata);

// Calls the attribute changed callback
void emAfClusterAttributeChangedCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t clientServerMask);

// Calls the attribute changed callback for a specific cluster.
EmberAfStatus emAfClusterPreAttributeChangedCallback(const chip::app::ConcreteAttributePath & attributePath,
                                                     uint8_t clientServerMask, EmberAfAttributeType attributeType, uint16_t size,
                                                     uint8_t * value);

// Calls the default response callback for a specific cluster.
// with the EMBER_NULL_MANUFACTURER_CODE
void emberAfClusterDefaultResponseCallback(chip::EndpointId endpoint, chip::ClusterId clusterId, chip::CommandId commandId,
                                           EmberAfStatus status, uint8_t clientServerMask);

// Calls the message sent callback for a specific cluster.
void emberAfClusterMessageSentCallback(const chip::MessageSendDestination & destination, EmberApsFrame * apsFrame, uint16_t msgLen,
                                       uint8_t * message, EmberStatus status);

// Checks a cluster mask byte against ticks passed bitmask
// returns true if the mask matches a passed interval
bool emberAfCheckTick(EmberAfClusterMask mask, uint8_t passedMask);

bool emberAfEndpointIsEnabled(chip::EndpointId endpoint);

// Note the difference in implementation from emberAfGetNthCluster().
// emberAfGetClusterByIndex() retrieves the cluster by index regardless of server/client
// and those indexes may be DIFFERENT than the indexes returned from
// emberAfGetNthCluster().  In other words:
//
//  - Use emberAfGetClustersFromEndpoint()  with emberAfGetNthCluster() emberAfGetNthClusterId()
//  - Use emberAfGetClusterCountForEndpoint() with emberAfGetClusterByIndex()
//
// Don't mix them.
uint8_t emberAfGetClusterCountForEndpoint(chip::EndpointId endpoint);
EmberAfCluster * emberAfGetClusterByIndex(chip::EndpointId endpoint, uint8_t clusterIndex);

uint16_t emberAfGetDeviceIdForEndpoint(chip::EndpointId endpoint);
EmberAfStatus emberAfSetDynamicEndpoint(uint16_t index, chip::EndpointId id, EmberAfEndpointType * ep, uint16_t deviceId,
                                        uint8_t deviceVersion);
chip::EndpointId emberAfClearDynamicEndpoint(uint16_t index);
uint16_t emberAfGetDynamicIndexFromEndpoint(chip::EndpointId id);

// Get the number of attributes of the specific cluster under the endpoint.
// Returns 0 if the cluster does not exist.
uint16_t emberAfGetServerAttributeCount(chip::EndpointId endpoint, chip::ClusterId cluster);

// Get the index of the given attribute of the specific cluster under the endpoint.
// Returns UINT16_MAX if the attribute does not exist.
uint16_t emberAfGetServerAttributeIndexByAttributeId(chip::EndpointId endpoint, chip::ClusterId cluster,
                                                     chip::AttributeId attributeId);

// Get the attribute id at the attributeIndex of the cluster under the endpoint. This function is useful for iterating over the
// attributes.
// Returns Optional<chip::AttributeId>::Missing() if the attribute does not exist.
chip::Optional<chip::AttributeId> emberAfGetServerAttributeIdByIndex(chip::EndpointId endpoint, chip::ClusterId cluster,
                                                                     uint16_t attributeIndex);

/**
 * Register an attribute access override.  It will remain registered until
 * the endpoint it's registered for is disabled (or until shutdown if it's
 * registered for all endpoints).  Registration will fail if there is an
 * already-registered override for the same set of attributes.
 *
 * @return false if there is an existing override that the new one would
 *               conflict with.  In this case the override is not registered.
 * @return true if registration was successful.
 */
bool registerAttributeAccessOverride(chip::app::AttributeAccessInterface * attrOverride);

/**
 * Find an attribute access override, if any, that is registered for the given
 * endpoint and cluster id.  This might be an override specific to the given
 * endpoint, or might be one registered for all endpoints.
 */
chip::app::AttributeAccessInterface * findAttributeAccessOverride(chip::EndpointId endpointId, chip::ClusterId clusterId);
