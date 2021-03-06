// Copyright 2019 Shift Cryptosecurity AG
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "commander_btc.h"
#include "commander_states.h"

#include <stdio.h>

#include <apps/btc/btc.h>
#include <apps/btc/btc_common.h>
#include <apps/btc/btc_sign.h>
#include <workflow/verify_pub.h>

#include <wally_bip32.h> // for BIP32_INITIAL_HARDENED_CHILD

static commander_error_t _result(app_btc_result_t result)
{
    switch (result) {
    case APP_BTC_OK:
        return COMMANDER_OK;
    case APP_BTC_ERR_USER_ABORT:
        return COMMANDER_ERR_USER_ABORT;
    case APP_BTC_ERR_INVALID_INPUT:
        return COMMANDER_ERR_INVALID_INPUT;
    case APP_BTC_ERR_DUPLICATE:
        return COMMANDER_ERR_DUPLICATE;
    default:
        return COMMANDER_ERR_GENERIC;
    }
}

static commander_error_t _btc_pub_xpub(const BTCPubRequest* request, PubResponse* response)
{
    if (!app_btc_xpub(
            request->coin,
            request->output.xpub_type,
            request->keypath,
            request->keypath_count,
            response->pub,
            sizeof(response->pub))) {
        return COMMANDER_ERR_GENERIC;
    }
    if (request->display) {
        char title[100] = {0};
        int n_written;
        switch (request->output.xpub_type) {
        case BTCPubRequest_XPubType_TPUB:
        case BTCPubRequest_XPubType_XPUB:
        case BTCPubRequest_XPubType_YPUB:
        case BTCPubRequest_XPubType_ZPUB:
        case BTCPubRequest_XPubType_VPUB:
        case BTCPubRequest_XPubType_UPUB:
        case BTCPubRequest_XPubType_CAPITAL_VPUB:
        case BTCPubRequest_XPubType_CAPITAL_ZPUB:
            n_written = snprintf(
                title,
                sizeof(title),
                "%s\naccount #%lu",
                btc_common_coin_name(request->coin),
                (unsigned long)request->keypath[2] - BIP32_INITIAL_HARDENED_CHILD + 1);
            if (n_written < 0 || n_written >= (int)sizeof(title)) {
                /*
                 * The message was truncated, or an error occurred.
                 * We don't want to display it: there could
                 * be some possibility for deceiving the user.
                 */
                return COMMANDER_ERR_GENERIC;
            }
            break;
        default:
            return COMMANDER_ERR_GENERIC;
        }
        if (!workflow_verify_pub(title, response->pub)) {
            return COMMANDER_ERR_USER_ABORT;
        }
    }
    return COMMANDER_OK;
}

static commander_error_t _btc_pub_address_simple(
    const BTCPubRequest* request,
    PubResponse* response)
{
    if (!app_btc_address_simple(
            request->coin,
            request->output.script_config.config.simple_type,
            request->keypath,
            request->keypath_count,
            response->pub,
            sizeof(response->pub))) {
        return COMMANDER_ERR_GENERIC;
    }
    if (request->display) {
        const char* coin = btc_common_coin_name(request->coin);
        char title[100] = {0};
        int n_written;
        switch (request->output.script_config.config.simple_type) {
        case BTCScriptConfig_SimpleType_P2WPKH_P2SH:
            n_written = snprintf(title, sizeof(title), "%s", coin);
            break;
        case BTCScriptConfig_SimpleType_P2WPKH:
            n_written = snprintf(title, sizeof(title), "%s bech32", coin);
            break;
        default:
            return COMMANDER_ERR_GENERIC;
        }
        if (n_written < 0 || n_written >= (int)sizeof(title)) {
            /*
             * The message was truncated, or an error occurred.
             * We don't want to display it: there could
             * be some possibility for deceiving the user.
             */
            return COMMANDER_ERR_GENERIC;
        }
        if (!workflow_verify_pub(title, response->pub)) {
            return COMMANDER_ERR_USER_ABORT;
        }
    }
    return COMMANDER_OK;
}

static commander_error_t _btc_pub_address_multisig(
    const BTCPubRequest* request,
    PubResponse* response)
{
    const BTCScriptConfig_Multisig* multisig = &request->output.script_config.config.multisig;
    app_btc_result_t result = app_btc_address_multisig_p2wsh(
        request->coin,
        multisig,
        request->keypath,
        request->keypath_count,
        response->pub,
        sizeof(response->pub),
        request->display);
    return _result(result);
}

commander_error_t commander_btc_pub(const BTCPubRequest* request, PubResponse* response)
{
    if (!app_btc_enabled(request->coin)) {
        return COMMANDER_ERR_DISABLED;
    }
    switch (request->which_output) {
    case BTCPubRequest_xpub_type_tag:
        return _btc_pub_xpub(request, response);
    case BTCPubRequest_script_config_tag:
        switch (request->output.script_config.which_config) {
        case BTCScriptConfig_simple_type_tag:
            return _btc_pub_address_simple(request, response);
        case BTCScriptConfig_multisig_tag:
            return _btc_pub_address_multisig(request, response);
        default:
            return COMMANDER_ERR_INVALID_INPUT;
        }
    default:
        return COMMANDER_ERR_INVALID_INPUT;
    }
}

commander_error_t commander_btc_sign(const Request* request, Response* response)
{
    response->which_response = Response_btc_sign_next_tag;
    app_btc_sign_error_t result;
    switch (request->which_request) {
    case Request_btc_sign_init_tag:
        if (!app_btc_enabled(request->request.btc_sign_init.coin)) {
            return COMMANDER_ERR_DISABLED;
        }
        result =
            app_btc_sign_init(&(request->request.btc_sign_init), &response->response.btc_sign_next);
        break;
    case Request_btc_sign_input_tag:
        result = app_btc_sign_input(
            &(request->request.btc_sign_input), &response->response.btc_sign_next);
        break;
    case Request_btc_sign_output_tag:
        result = app_btc_sign_output(
            &(request->request.btc_sign_output), &response->response.btc_sign_next);
        break;
    default:
        return COMMANDER_ERR_GENERIC;
    }
    if (result == APP_BTC_SIGN_ERR_USER_ABORT) {
        return COMMANDER_ERR_USER_ABORT;
    }
    if (result != APP_BTC_SIGN_OK) {
        return COMMANDER_ERR_GENERIC;
    }
    switch (response->response.btc_sign_next.type) {
    case BTCSignNextResponse_Type_INPUT:
        commander_states_force_next(Request_btc_sign_input_tag);
        break;
    case BTCSignNextResponse_Type_OUTPUT:
        commander_states_force_next(Request_btc_sign_output_tag);
        break;
    default:
        break;
    }
    return COMMANDER_OK;
}

static commander_error_t _api_is_script_config_registered(
    const BTCIsScriptConfigRegisteredRequest* request,
    BTCIsScriptConfigRegisteredResponse* response)
{
    const BTCScriptConfigRegistration* reg = &request->registration;
    if (!app_btc_is_script_config_registered(
            reg->coin,
            &reg->script_config,
            reg->keypath,
            reg->keypath_count,
            &response->is_registered)) {
        return COMMANDER_ERR_GENERIC;
    }
    return COMMANDER_OK;
}

static commander_error_t _api_register_script_config(const BTCRegisterScriptConfigRequest* request)
{
    app_btc_result_t result = app_btc_register_script_config(
        request->registration.coin,
        &request->registration.script_config,
        request->registration.keypath,
        request->registration.keypath_count,
        request->name);
    return _result(result);
}

commander_error_t commander_btc(const BTCRequest* request, BTCResponse* response)
{
    switch (request->which_request) {
    case BTCRequest_is_script_config_registered_tag:
        response->which_response = BTCResponse_is_script_config_registered_tag;
        return _api_is_script_config_registered(
            &(request->request.is_script_config_registered),
            &response->response.is_script_config_registered);
    case BTCRequest_register_script_config_tag:
        response->which_response = BTCResponse_success_tag;
        return _api_register_script_config(&(request->request.register_script_config));
    default:
        return COMMANDER_ERR_GENERIC;
    }
    return COMMANDER_OK;
}
