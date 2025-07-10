#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# app_setup.py
#
# Write setup to application persistent area for application setup SDK library
#
# Values to be stored
#
# - Node id
# - Node role
# - Network address
# - Network channel
# - Diagnostic interval
# - Network key pair (authentication, encryption)
# - Management key pair (authentication, encryption)
# - Network key pair sequence
# - Management key pair sequence
# - Flag to enable key management
# - Flag to force revokation of management keys
# - Provisioning method
# - Provisioning number of retries
# - Provisioning timeout
# - Provisioning UID
# - Provisioning keys
# - Flag to start DualMCU app automatically
# - Flag whether application persistent area should be erased after reading
#
# The memory layout of application setup:
#
# uint32_t      format    // 0xabb5e70b marks application setup data
# uint16_t      version   // data version
# uint16_t      length    // length in bytes of header + data
# uint32_t      node_id
# uint32_t      network_address
# uint8_t       node_role
# uint8_t       network_channel
# uint8_t       diag_interval
# uint8_t       reserved
# uint8_t[16]   network_enc_key
# uint8_t[16]   network_auth_key
# uint8_t[16]   mgmt_enc_key
# uint8_t[16]   mgmt_auth_key
# uint8_t       network_key_pair_seq
# uint8_t       mgmt_key_pair_seq
# uint8_t       enable_key_management
# uint8_t       force_key_revokation
# uint8_t       provision_method
# uint8_t       provision_num_retries
# uint16_t      provision_timeout
# uint8_t[16]   provision_enc_key
# uint8_t[16]   provision_auth_key
# uint8_t[112]  provision_uid
# uint8_t       provision_uid_len
# uint8_t[3]    reserved
# uint8_t       start_dualmcu
# uint8_t       erase_after
# uint16_t      reserved

import sys
import argparse
from intelhex import IntelHex
import binascii

SETUP_VERSION        = 1
SETUP_SIZE           = 244
SETUP_FORMAT         = 0xabb5e70b
SETUP_UID_MAX_LEN    = 112

# The image flashed to application persistent area consist of
# APP_PERSISTENT_MAGIC and the setup
APP_PERSISTENT_MAGIC = 0x1E75FED8
IMAGE_SIZE  = 248

# Content of these dicts comes from git/app-interface/tools/scratchpad_*.ini files.
# Make sure these dicts are up to date!
app_persistent_area_addresses = { 'efr32xg12pxxxf512':   0x0007B000,
                                  'efr32xg12pxxxf1024':  0x000FB000,
                                  'efr32xg13pxxxf512':   0x0007B000,
                                  'efr32xg21xxxxf1024':  0x000F8000,
                                  'efr32xg21xxxxf768':   0x000B8000,
                                  'efr32xg21xxxxf512':   0x00078000,
                                  'efr32xg22xxxxf512':   0x00078000,
                                  'efr32xg23xxxxf512':   0x08078000,
                                  'efr32xg24xxxxf1536':  0x08178000,
                                  'efr32xg24xxxxf1024':  0x080F8000,
                                  'nrf52832':            0x0007a000,
                                  'nrf52833':            0x0007a000,
                                  'nrf52840':            0x000fa000,
                                  'nrf9161':             0x000fa000,
                                  'nrf54l05':            0x00077000,
                                  'nrf54l10':            0x000f9800,
                                  'nrf54l15':            0x00177000,
                                }
app_persistent_area_lengths = { 'efr32xg12pxxxf512':   16384,
                                'efr32xg12pxxxf1024':  16384,
                                'efr32xg13pxxxf512':   16384,
                                'efr32xg21xxxxf1024':  16384,
                                'efr32xg21xxxxf768':   16384,
                                'efr32xg21xxxxf512':   16384,
                                'efr32xg22xxxxf512':   16384,
                                'efr32xg23xxxxf512':   16384,
                                'efr32xg24xxxxf1536':  16384,
                                'efr32xg24xxxxf1024':  16384,
                                'nrf52832':            16384,
                                'nrf52833':            16384,
                                'nrf52840':            16384,
                                'nrf9161':             16384,
                                'nrf54l05':            16384,
                                'nrf54l10':            16384,
                                'nrf54l15':            16384,
                              }

persistent_roles = { '0':                      0,
                     'LE_SINK':                0,
                     '1':                      1,
                     'LE_HEADNODE':            1,
                     '2':                      2,
                     'LE_SUBNODE':             2,
                     '4':                      4,
                     'ADVERTISER':             4,
                     '16':                    16,
                     'LL_SINK':               16,
                     '17':                    17,
                     'LL_HEADNODE':           17,
                     '18':                    18,
                     'LL_SUBNODE':            18,
                     '66':                    66,
                     'LE_AUTOROLE':           66,
                     '82':                    82,
                     'LL_AUTOROLE':           82,
                   }

provisioning_methods = { '0': 0,
                         'UNSECURED' : 0,
                         '1': 1,
                         'SECURED': 1,
                         '3': 3,
                         'EXTENDED_UID': 3,
                         '4': 4,
                         'KEY_MGMT': 4,
                        }

def inject_config_to_hex(nodeId, args):

    app_persistent_area_address = app_persistent_area_addresses[args.device]
    app_persistent_area_length = app_persistent_area_lengths[args.device]

    # Load hex file into memory
    ih = IntelHex(args.hexfile)

    # Fill whole persistent area with 0xFF (virtually the same as erasing it)
    for offset in range(app_persistent_area_length):
        ih[app_persistent_area_address + offset] = 0xFF

    # Initialize config array with 0xff
    config = bytearray(IMAGE_SIZE * b'\xFF' )

    # App persistent magic
    config[0:4] = (APP_PERSISTENT_MAGIC).to_bytes(4, byteorder='little')

    # Header marking data to follow to be injected config
    config[4:8] = (SETUP_FORMAT).to_bytes(4, byteorder='little')

    # Data version
    config[8:10] = (SETUP_VERSION).to_bytes(2, byteorder='little')

    # Data size
    config[10:12] = (SETUP_SIZE).to_bytes(2, byteorder='little')

    # Node address
    if nodeId is not None:
        config[12:16] = int(nodeId).to_bytes(4, byteorder='little')

    # Network address
    if args.networkAddress is not None:
        config[16:20] = int(args.networkAddress).to_bytes(4, byteorder='little')

    # Node role
    if args.nodeRole is not None:
        config[20] = persistent_roles[args.nodeRole]

    # Network channel
    if args.networkChannel is not None:
        config[21] = int(args.networkChannel)

    # Diagnostic interval
    if args.diagnosticInterval is not None:
        config[22] = int(args.diagnosticInterval)

    # Legacy encryption key
    if args.encryptionKey is not None:
        config[24:40] = binascii.unhexlify(args.encryptionKey)

    # Legacy authentication key
    if args.authenticationKey is not None:
        config[40:56] = binascii.unhexlify(args.authenticationKey)

    if args.mgmtEncryptionKey is not None:
        config[56:72] = binascii.unhexlify(args.mgmtEncryptionKey)

    if args.mgmtAuthenticationKey is not None:
        config[72:88] = binascii.unhexlify(args.mgmtAuthenticationKey)

    if args.netKeyPairSeq is not None:
        config[88] = int(args.netKeyPairSeq)

    if args.mgmtKeyPairSeq is not None:
        config[89] = int(args.mgmtKeyPairSeq)

    if args.enableKeyManagement is not None:
        config[90] = int(args.enableKeyManagement)

    if args.forceKeyRevocation is not None:
        config[91] = int(args.forceKeyRevocation)

    if args.provisionMethod is not None:
        config[92] = provisioning_methods[args.provisionMethod]

    if args.provisionNumRetries is not None:
        config[93] = int(args.provisionNumRetries)

    if args.provisionTimeout is not None:
        config[94:96] = int(args.provisionTimeout).to_bytes(2, byteorder='little')

    if args.provisionEncryptionKey is not None:
        if provisioning_methods[args.provisionMethod] != provisioning_methods['SECURED']:
            sys.exit("Provision keys need the method SECURED")

        config[96:112] = binascii.unhexlify(args.provisionEncryptionKey)

    if args.provisionAuthenticationKey is not None:
        if provisioning_methods[args.provisionMethod] != provisioning_methods['SECURED']:
            sys.exit("Provision keys need the method SECURED")

        config[112:128] = binascii.unhexlify(args.provisionAuthenticationKey)

    if args.provisionUid is not None:
        uidLen = len(args.provisionUid)
        if (uidLen > SETUP_UID_MAX_LEN):
            sys.exit("UID len {} > SETUP_UID_MAX_LEN({})".format(uidLen,SETUP_UID_MAX_LEN))
        lastIndex = 128 + uidLen
        config[128:lastIndex] = binascii.unhexlify(args.provisionUid)
        config[240] = uidLen

    if args.startDualMCU is not None:
        config[244] = int(args.startDualMCU)

    if args.noEraseAfter is not None:
        config[245] = int(args.noEraseAfter)

    # Copy the config to hex file
    for offset in range(IMAGE_SIZE):
        ih[app_persistent_area_address + offset] = config[offset]

    if args.extraHexFiles:
        ehfs = args.extraHexFiles.split(',')
        for ehf in ehfs:
            ehfih = IntelHex(ehf)
            ih.merge(ehfih, overlap='replace')

    if nodeId is not None:
        ih.write_hex_file(args.hexfile.rsplit('.',1)[0]+"_with_params_{}.hex".format(nodeId))
    else:
        ih.write_hex_file(args.hexfile.rsplit('.',1)[0]+"_with_params.hex")

# Main
if __name__ == '__main__':

    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--hexfile',
                        default = None,
                        help='hex file to flash',
                        required=True)
    parser.add_argument('-d', '--device',
                        choices = ['efr32xg12pxxxf512', 'efr32xg12pxxxf1024', 'efr32xg13pxxxf512',
                                  'efr32xg21xxxxf1024', 'efr32xg21xxxxf768', 'efr32xg21xxxxf512',
                                  'efr32xg22xxxxf512', 'efr32xg23xxxxf512',
                                  'efr32xg24xxxxf1536','efr32xg24xxxxf1024',
                                  'nrf52832', 'nrf52833', 'nrf52840', 'nrf9160', 'nrf9161',
                                  'nrf54l05', 'nrf54l10', 'nrf54l15' ],
                        default = None,
                        help = 'device',
                        required=True,
                        type=str.lower)
    parser.add_argument('-l', '--nodeAddresses',
                        default = None,
                        help='list of node addresses, e.g. 1-10,21-30, unique hwid is used if empty',
                        required=False)
    parser.add_argument('-n', '--networkAddress',
                        default = None,
                        help='address of the network',
                        required=False)
    parser.add_argument('-r', '--nodeRole',
                        choices = [ '0', 'LE_SINK',
                                    '1', 'LE_HEADNODE',
                                    '2', 'LE_SUBNODE',
                                    '4', 'ADVERTISER',
                                   '16', 'LL_SINK',
                                   '17', 'LL_HEADNODE',
                                   '18', 'LL_SUBNODE',
                                   '66', 'LE_AUTOROLE',
                                   '82', 'LL_AUTOROLE' ],
                        default = None,
                        help='role of the node',
                        required=False)
    parser.add_argument('-c', '--networkChannel',
                        default = None,
                        help='network channel',
                        required=False)
    parser.add_argument('-i', '--diagnosticInterval',
                        choices = ['0', '30', '60', '120', '300', '600', '1800'],
                        default = '0',
                        help='diagnostic interval',
                        required=False)
    parser.add_argument('-e', '--encryptionKey',
                        default = None,
                        help='encryption key',
                        required=False)
    parser.add_argument('-a', '--authenticationKey',
                        default = None,
                        help='authentication key',
                        required=False)
    parser.add_argument('-me', '--mgmtEncryptionKey',
                        default = None,
                        help='RFU: management encryption key',
                        required=False)
    parser.add_argument('-ma', '--mgmtAuthenticationKey',
                        default = None,
                        help='RFU: management authentication key',
                        required=False)
    parser.add_argument('-sn', '--netKeyPairSeq',
                        default = 1,
                        help='RFU: network key pair sequence num',
                        required=False)
    parser.add_argument('-msn', '--mgmtKeyPairSeq',
                        default = 1,
                        help='RFU: management key pair sequence number',
                        required=False)
    parser.add_argument('-ekm', '--enableKeyManagement',
                        default = 0,
                        help='RFU: enable key management',
                        required=False)
    parser.add_argument('-ar', '--forceKeyRevocation',
                        action='store_true', # False if not given, True if given
                        help='Force key revocation',
                        required=False)
    parser.add_argument('-pm', '--provisionMethod',
                        choices = [ '0', 'UNSECURED',
                                    '1', 'SECURED',
                                    '3', 'EXTENDED_UID',
                                    '4', 'KEY_MGMT' ],
                        default = None,
                        help='Provisioning method',
                        required=False)
    parser.add_argument('-pr', '--provisionNumRetries',
                        default = 3,
                        help='Provisioning number of retries',
                        required=False)
    parser.add_argument('-pt', '--provisionTimeout',
                        default = 60,
                        help='Provisioning timeout in seconds',
                        required=False)
    parser.add_argument('-puid', '--provisionUid',
                        default = None,
                        help='Provisioning UID',
                        required=False)
    parser.add_argument('-pe', '--provisionEncryptionKey',
                        default = None,
                        help='Provision encryption key',
                        required=False)
    parser.add_argument('-pa', '--provisionAuthenticationKey',
                        default = None,
                        help='Provision authentication key',
                        required=False)
    parser.add_argument('-s', '--startDualMCU',
                        default = None,
                        help='Autostart dualmcu_app',
                        required=False)
    parser.add_argument('-ner', '--noEraseAfter',
                        action='store_true',
                        help='Do not erase data after first boot',
                        required=False)
    parser.add_argument('-x', '--extraHexFiles',
                        default = None,
                        help='Extra hex files to be merged',
                        required=False)

    args = parser.parse_args()

    nodes = []

    if args.nodeAddresses is not None:

        if "-" in args.nodeAddresses:
            for splitted_list in map(str, args.nodeAddresses.split(',')):
                if "-" in splitted_list:
                    node_range = splitted_list.split("-")
                    for node in range(int(node_range[0]), int(node_range[1])+1):
                        nodes.append(int(node))
                else:
                    nodes.append(int(splitted_list))
        else:
            nodes.extend(set(map(int, args.nodeAddresses.split(','))))

        for nodeId in nodes:
            inject_config_to_hex(nodeId, args)

    else:
        inject_config_to_hex(None, args)
