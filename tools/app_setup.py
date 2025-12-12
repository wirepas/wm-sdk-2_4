#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# app_setup.py
#
# Write setup to application persistent area for application setup SDK library
#
# Divided into node and network specific settings.
#
# Only one device can be set by one run of the script.
#
# The memory layout of application setup:
#
# HEADER (8 bytes)
# uint32_t      format    // 0xabb5e70b marks application setup data
# uint16_t      version   // data version
# uint16_t      length    // length in bytes of header + data

# NODE SETTINGS (8 bytes)
# uint32_t      node_address
# uint8_t       node_role
# uint16_t      diag_interval (only if role is LL_SINK or LE_SINK)
# uint8_t       reserved

# NETWORK SETTINGS (72 bytes)
# uint32_t      network_address
# uint8_t       network_channel (<=40 for ISM24, <=14 for DECT, <=12 for SUBG)
# uint8_t       network_key_pair_seq (default:1 if provision method EUID_KEY_MGMT, not allowed if method not EUID_KEY_MGMT)
# uint8_t       mgmt_key_pair_seq (default:1 if provision method EUID_KEY_MGMT, not allowed if method not EUID_KEY_MGMT)
# uint8_t       reserved
# uint8_t[16]   network_enc_key (required if provision method EUID_KEY_MGMT, otherwise allowed)
# uint8_t[16]   network_auth_key (required if provision method EUID_KEY_MGMT, othewise allowed)
# uint8_t[16]   mgmt_enc_key (required if provision method EUID_KEY_MGMT, otherwise not allowed)
# uint8_t[16]   mgmt_auth_key (required if provision method EUID_KEY_MGMT, otherwise not allowed)

# PROVISIONING SETTINGS (116 bytes)
# uint8_t       provision_method
# uint8_t       provision_num_retries (default: 3)
# uint16_t      provision_timeout (defaults: 10 (LL), 60 (LE)
# uint8_t[16]   provision_enc_key (required if provision_method is secured)
# uint8_t[16]   provision_auth_key (required if provision_method is secured)
# uint8_t[79]   provisioning_uid
# uint8_t       provisioning_uid_len

# ACTIONS (4 bytes)
# uint8_t       start_dualmcu
# uint8_t       preserve data (do not wipe app persistent area after first boot)
# uint8_t[2]    reserved

import sys
import argparse
import json
import binascii
from intelhex import IntelHex
from dataclasses import dataclass
from enum import Enum

SETUP_VERSION       = 2
SETUP_SIZE_BYTES    = 208  # = HEADER SIZE + NODE SETTINGS SIZE + NETWORK SETTINGS SIZE + PROVISIONING SETTINGS SIZE + ACTIONS SIZE
SETUP_FORMAT        = 0xabb5e70b

SEC_KEY_LEN_BYTES = 16
KEY_SEQ_MAX       = 255

NET_KEYS_SEQ_DEFAULT = 1
MGMT_KEYS_SEQ_DEFAULT = 1

PROV_UID_MAX_LEN = 79
PROV_EUID_LEN    = 16

PROV_UID_DEFAULT_TYPE = 1
PROV_AUTH_UID_LEN     = 16

PROV_UID_TYPE_MIN = 0x01
AUTH_UID_TYPE_MIN = 0x01
PROV_UID_TYPE_MAX = 0x01
AUTH_UID_TYPE_MAX = 0x01

# The image flashed to application persistent area consist of
# APP_PERSISTENT_MAGIC and the setup
APP_PERSISTENT_MAGIC        = 0x1E75FED8
APP_PERSISTENT_MAGIC_LEN    = 4
APP_PERSISTENT_LEN          = 16384

IMAGE_SIZE = SETUP_SIZE_BYTES + APP_PERSISTENT_MAGIC_LEN

# Max channel is dependenct on device used
CHANNEL_MAX_ISM24       = 40
CHANNEL_MAX_SUBG        = 12
CHANNEL_MAX_DECT_BAND1  = 11
CHANNEL_MAX_DECT_BAND4  = 14
CHANNEL_MAX_DECT_BAND9  = 5



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

radio_max_ch = { 'efr32xg12pxxxf512':   CHANNEL_MAX_ISM24,
                 'efr32xg12pxxxf1024':  CHANNEL_MAX_ISM24,
                 'efr32xg13pxxxf512':   CHANNEL_MAX_SUBG,
                 'efr32xg21xxxxf1024':  CHANNEL_MAX_ISM24,
                 'efr32xg21xxxxf768':   CHANNEL_MAX_ISM24,
                 'efr32xg21xxxxf512':   CHANNEL_MAX_ISM24,
                 'efr32xg22xxxxf512':   CHANNEL_MAX_ISM24,
                 'efr32xg23xxxxf512':   CHANNEL_MAX_SUBG,
                 'efr32xg24xxxxf1536':  CHANNEL_MAX_ISM24,
                 'efr32xg24xxxxf1024':  CHANNEL_MAX_ISM24,
                 'nrf52832':            CHANNEL_MAX_ISM24,
                 'nrf52833':            CHANNEL_MAX_ISM24,
                 'nrf52840':            CHANNEL_MAX_ISM24,
                 'nrf9161':             CHANNEL_MAX_DECT_BAND1,
                 'nrf9151':             CHANNEL_MAX_DECT_BAND4,
                 'nrf54l05':            CHANNEL_MAX_ISM24,
                 'nrf54l10':            CHANNEL_MAX_ISM24,
                 'nrf54l15':            CHANNEL_MAX_ISM24,
                  }

NODE_ROLE = { '0':                      0,
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

PROV_METHOD = { '0': 0,
                'UNSECURED' : 0,
                '1': 1,
                'SECURED': 1,
                '3': 3,
                'EUID': 3,
                '7': 7,
                'EUID_KEY_MGMT': 7
                }

class ProvisioningUIDType(Enum):
    NONE = 0
    UID = 1
    EUID = 2

@dataclass
class AppSetupConfig:
    provisioning_method : str
    device              : str

@dataclass
class Header:

    @staticmethod
    def to_bytes() -> bytes:

        output = bytes()
        output += (SETUP_FORMAT).to_bytes(4, byteorder='little')
        output += (SETUP_VERSION).to_bytes(2, byteorder='little')
        output += (SETUP_SIZE_BYTES).to_bytes(2, byteorder='little')

        return output

@dataclass
class NodeSettings:
    node_address:  int
    node_role:     str
    diag_ival:     int

    @staticmethod
    def loadArguments(parser : argparse.ArgumentParser) -> None:

        parser.add_argument(
            '-a', '--nodeAddress', default = None, required = False,
            type = int,
            help = "Unique node address as 32-bit number"
        )

        parser.add_argument(
            '-r', '--nodeRole', default = None, required = False,
            type = str,
            choices = [ '0', 'LE_SINK',
                        '1', 'LE_HEADNODE',
                        '2', 'LE_SUBNODE',
                        '4', 'ADVERTISER',
                       '16', 'LL_SINK',
                       '17', 'LL_HEADNODE',
                       '18', 'LL_SUBNODE',
                       '66', 'LE_AUTOROLE',
                       '82', 'LL_AUTOROLE' ],
            help = "Node role as string"
        )

        parser.add_argument(
            '-di', '--diagInterval', default = None, required = False,
            type = int,
            choices = [0, 30, 60, 120, 300, 600, 1800],
            help = "Diagnostic interval as seconds for sink role"
        )

    @staticmethod
    def fromArgs(args : argparse.Namespace):

        return NodeSettings.fromDict(dict({
            'a'  : args.nodeAddress,
            'r'  : args.nodeRole,
            'di' : args.diagInterval,
        }))

    @staticmethod
    def fromDict(params: dict):

        return NodeSettings(json.dumps(params))

    def __init__(self, params : str):

        cfg = json.loads(params)

        address_present = bool("a" in cfg.keys() and cfg["a"] is not None)
        self.node_address = int(cfg["a"]) if address_present else None

        role_present = bool("r" in cfg.keys() and cfg["r"] is not None)
        self.node_role     = str(cfg["r"]) if role_present else None

        diag_ival_present = bool("di" in cfg.keys() and cfg["di"] is not None)
        self.diag_ival     = int(cfg["di"]) if diag_ival_present else None

    def validate(self) -> None:

        if (self.node_address is not None and self.node_address < 1):

            raise ValueError("Node address must be positive integer")

        if (self.node_role is not None
                and self.node_role not in NODE_ROLE.keys()):

            raise ValueError(f"Unknown role {self.node_role}")

        if (self.diag_ival is not None
            and (self.node_role is None
                or self.node_role not in NODE_ROLE.keys()
                or (NODE_ROLE[self.node_role] != NODE_ROLE['LL_SINK']
                    and NODE_ROLE[self.node_role] != NODE_ROLE['LE_SINK']))):

            raise ValueError("Diagnostic interval given but role not sink.")

    def to_bytes(self) -> bytes:

        self.validate()

        output = bytes()

        output += self.node_address.to_bytes(4, byteorder='little') \
                    if self.node_address is not None else (0xffffffff).to_bytes(4)
        output += NODE_ROLE[self.node_role].to_bytes(1) \
                    if self.node_role is not None else (0xff).to_bytes(1)
        output += self.diag_ival.to_bytes(2, byteorder='little') \
                    if self.diag_ival is not None else (0xffff).to_bytes(2)
        output += (0xff).to_bytes(1) # Padding bytes

        return output

@dataclass
class NetworkSettings:
    net_addr:       int
    net_ch:         int
    net_enc_key:    str
    net_auth_key:   str
    mgmt_enc_key:   str
    mgmt_auth_key:  str
    net_keys_seq:   int
    mgmt_keys_seq:  int

    @staticmethod
    def loadArguments(parser : argparse.ArgumentParser) -> None:

        parser.add_argument(
            '-na', '--networkAddress', default = None, required = False,
            type = str,
            help = "32-bit address of the network, as int or 0x-prefixed hex string"
        )

        parser.add_argument(
            '-nc', '--networkChannel', default = None, required = False,
            type = int,
            help = "Network channel, profile specific limits for valid values."
        )

        parser.add_argument(
            '-nenc', '--networkEncKey', default = None, required = False,
            type = str,
            help = "16 byte network encryption key as hex string"
        )

        parser.add_argument(
            '-nauth', '--networkAuthKey', default = None, required = False,
            type = str,
            help = "16 byte network authentication key as hex string"
        )

        parser.add_argument(
            '-menc', '--mgmtEncKey', default = None, required = False,
            type = str,
            help = "16 byte management encryption key as hex string"
        )

        parser.add_argument(
            '-mauth', '--mgmtAuthKey', default = None, required = False,
            type = str,
            help = "16 byte management authentication key as hex string"
        )

        parser.add_argument(
            '-nseq', '--netKeyPairSeq', default = None, required = False,
            type = int,
            help = "1-byte network key pair sequence number for "
                    + "scalable key management (default: 0x01)"
        )

        parser.add_argument(
            '-mseq', '--mgmtKeyPairSeq', default = None, required = False,
            type = int,
            help = "1-byte management key pair sequence number for "
                    + "scalable key management (default: 0x01)"
        )

    @staticmethod
    def fromArgs(args : argparse.Namespace):

        return NetworkSettings.fromDict(dict({
            "na"    : args.networkAddress,
            "nc"    : args.networkChannel,
            "nenc"  : args.networkEncKey,
            "nauth" : args.networkAuthKey,
            "menc"  : args.mgmtEncKey,
            "mauth" : args.mgmtAuthKey,
            "nseq"  : args.netKeyPairSeq,
            "mseq"  : args.mgmtKeyPairSeq
        }))

    @staticmethod
    def fromDict(params : dict):

        return NetworkSettings(json.dumps(params))

    def __init__(self, params: str) -> None:

        cfg = json.loads(params)

        na_present = bool("na" in cfg.keys() and cfg["na"] is not None)
        self.net_addr = int(cfg["na"], 0) if na_present else None

        nc_present = bool("nc" in cfg.keys() and cfg["nc"] is not None)
        self.net_ch = int(cfg["nc"]) if nc_present else None

        nenc_present = bool("nenc" in cfg.keys() and cfg["nenc"] is not None)
        self.net_enc_key = str(cfg["nenc"]) if nenc_present else None

        nauth_present = bool("nauth" in cfg.keys() and cfg["nauth"] is not None)
        self.net_auth_key = str(cfg["nauth"]) if nauth_present else None

        menc_present = bool("menc" in cfg.keys() and cfg["menc"] is not None)
        self.mgmt_enc_key = str(cfg["menc"]) if menc_present else None

        mauth_present = bool("mauth" in cfg.keys() and cfg["mauth"] is not None)
        self.mgmt_auth_key = str(cfg["mauth"]) if mauth_present else None

        nseq_present = bool("nseq" in cfg.keys() and cfg["nseq"] is not None)
        self.net_keys_seq = int(cfg["nseq"]) if nseq_present else None

        mseq_present = bool("mseq" in cfg.keys() and cfg["mseq"] is not None)
        self.mgmt_keys_seq = int(cfg["mseq"]) if mseq_present else None

    def validate(self) -> None:

        method = g_config.provisioning_method

        mgmt_keys_allowed = bool(
                PROV_METHOD[method] == PROV_METHOD['EUID_KEY_MGMT']
        ) if method is not None and method in PROV_METHOD.keys() else False

        if (self.net_addr is not None and self.net_addr < 1):

            raise ValueError("Network address must be positive integer")

        if (self.net_ch is not None and
            (self.net_ch < 1 or self.net_ch > radio_max_ch[g_config.device])):

            raise ValueError(f"Network channel must be positive integer "
                              + f"between 1..{radio_max_ch[g_config.device]}")

        # Check key combinations

        if (self.net_enc_key is None and self.net_auth_key is not None):
            raise ValueError("Network authentication key requires "
                              + "network encryption key.")
        if (self.net_enc_key is not None and self.net_auth_key is None):
            raise ValueError("Network encryption key requires "
                              + "network authentication key.")
        if (self.mgmt_enc_key is None and self.mgmt_auth_key is not None):
            raise ValueError("Management authentication key requires "
                              + "management encryption key.")
        if (self.mgmt_enc_key is not None and self.mgmt_auth_key is None):
            raise ValueError("Management encryption key requires "
                              + "management authentication key.")

        # Network encryption key

        elif (self.net_enc_key is not None):

            key_len = len(binascii.unhexlify(self.net_enc_key))

            if (key_len != SEC_KEY_LEN_BYTES):

                raise ValueError(f"Network encryption key length {key_len}, "
                                  + f"must be {SEC_KEY_LEN_BYTES} bytes.")

        # Network authentication key

        elif (self.net_auth_key is not None):

            key_len = len(binascii.unhexlify(self.net_auth_key))

            if (key_len != SEC_KEY_LEN_BYTES):

                raise ValueError(f"Network authentication key length {key_len}, "
                                   + f"must be {SEC_KEY_LEN_BYTES} bytes.")

        # Management encryption key

        elif (mgmt_keys_allowed and self.mgmt_enc_key is not None):

            raise ValueError("Management keys allowed only if method is "
                             + "EUID_KEY_MGMT.")

        elif (self.mgmt_enc_key is not None):

            key_len = len(binascii.unhexlify(self.mgmt_enc_key))

            if (key_len != SEC_KEY_LEN_BYTES):

                raise ValueError(f"Management encryption key length {key_len}, "
                                  + f"must be {SEC_KEY_LEN_BYTES} bytes.")

        # Management authentication key

        elif (mgmt_keys_allowed and self.mgmt_auth_key is not None):

            raise ValueError("Management keys allowed only if method is "
                             + "EUID_KEY_MGMT.")

        elif (self.mgmt_auth_key is not None):

            key_len = len(binascii.unhexlify(self.mgmt_auth_key))

            if (key_len != SEC_KEY_LEN_BYTES):

                raise ValueError(f"Management authentication key length {key_len}, "
                                  + f"must be {SEC_KEY_LEN_BYTES} bytes.")

        # Network key pair sequence

        if (self.net_keys_seq is None):
            # Use default value for network key sequence if keys are given but
            # key sequence is not
            if (self.net_enc_key is not None and self.net_auth_key is not None):
                self.net_keys_seq = NET_KEYS_SEQ_DEFAULT

        elif (self.net_keys_seq is not None
              and (self.net_keys_seq < 1 or self.net_keys_seq > KEY_SEQ_MAX)):

                raise ValueError("Network key sequence must be positive "
                                  + f"integer between 1..{KEY_SEQ_MAX}.")

        # Management key pair sequence

        if (self.mgmt_enc_key is not None and self.mgmt_keys_seq is None):

            # Default value only when the sequence numbers are required
            self.mgmt_keys_seq = MGMT_KEYS_SEQ_DEFAULT

        elif (mgmt_keys_allowed == False and self.mgmt_keys_seq is not None):

            raise ValueError("Management key pair seq allowed only if "
                             + "method is EUID_KEY_MGMT.")

        elif (self.mgmt_keys_seq is not None
              and (self.mgmt_keys_seq < 1 or self.mgmt_keys_seq > KEY_SEQ_MAX)):

                raise ValueError("Management key sequence must be positive "
                                  + f"integer between 1..{KEY_SEQ_MAX}.")

    def to_bytes(self) -> bytes:

        self.validate()

        output = bytes()

        output += self.net_addr.to_bytes(4, byteorder='little') \
                    if self.net_addr is not None else (0xffffffff).to_bytes(4)
        output += self.net_ch.to_bytes(1) \
                    if self.net_ch is not None else (0xff).to_bytes(1)
        output += self.net_keys_seq.to_bytes(1) \
                    if self.net_keys_seq is not None else (0xff).to_bytes(1)
        output += self.mgmt_keys_seq.to_bytes(1) \
                    if self.mgmt_keys_seq is not None else (0xff).to_bytes(1)
        output += (0xff).to_bytes(1) # Padding byte
        output += binascii.unhexlify(self.net_enc_key) \
                    if self.net_enc_key is not None \
                    else bytes([0xff for i in range(SEC_KEY_LEN_BYTES)])
        output += binascii.unhexlify(self.net_auth_key) \
                    if self.net_auth_key is not None \
                    else bytes([0xff for i in range(SEC_KEY_LEN_BYTES)])
        output += binascii.unhexlify(self.mgmt_enc_key) \
                    if self.mgmt_enc_key is not None \
                    else bytes([0xff for i in range(SEC_KEY_LEN_BYTES)])
        output += binascii.unhexlify(self.mgmt_auth_key) \
                    if self.mgmt_auth_key is not None \
                    else bytes([0xff for i in range(SEC_KEY_LEN_BYTES)])

        return output

@dataclass
class ProvisioningSettings:
    method:        str
    retries:       int
    timeout:       int
    enc_key:       str
    auth_key:      str
    prov_uid:      str
    auth_uid_type: int
    auth_uid:      str
    node_uid_type: int
    node_uid:      int

    @staticmethod
    def loadArguments(parser : argparse.ArgumentParser) -> None:

        parser.add_argument(
            '-pm', '--provisionMethod', default = None, required = False,
            type = str,
            help = "Provision method",
            choices = [ '0', 'UNSECURED',
                        '1', 'SECURED',
                        '3', 'EUID',
                        '7', 'EUID_KEY_MGMT' ],
        )

        parser.add_argument(
            '-pr', '--provisionRetries', default = None, required = False,
            type = int,
            help = "How many retries are attempted. Default: 3"
        )

        parser.add_argument(
            '-pt', '--provisionTimeout', default = None, required = False,
            type = int,
            help = "Provision timeout. Defaults: 10 (LL) & 60 (LE)"
        )

        parser.add_argument(
            '-penc', '--provisionEncKey', default = None, required = False,
            type = str,
            help = "16 byte provision encryption key as hex string"
        )

        parser.add_argument(
            '-pauth', '--provisionAuthKey', default = None, required = False,
            type = str,
            help = "16 byte provision authentication key as hex string"
        )
        parser.add_argument(
            '-pid', '--provisionUid', default = None, required = False,
            type = str,
            help = "Provisioning UID as hex string with length of 1-79 bytes"
        )
        parser.add_argument(
            '-aidt', '--authenticatorUidType', default = None, required = False,
            type = int,
            help = "Authenticator UID type as integer"
        )
        parser.add_argument(
            '-aid', '--authenticatorUid', default = None, required = False,
            type = str,
            help = "Authenticator UID as 16 byte long hex string, dashes optional"
        )
        parser.add_argument(
            '-nidt', '--nodeUidType', default = None, required = False,
            type = int,
            help = "Node UID type as integer"
        )
        parser.add_argument(
            '-nid', '--nodeUid', default = None, required = False,
            type = str,
            help = "Node UID as 16 byte long hex string, dashes optional"
        )


    @staticmethod
    def fromArgs(args : argparse.Namespace):

        return ProvisioningSettings.fromDict(dict({
            "pm"    : args.provisionMethod,
            "pr"    : args.provisionRetries,
            "pt"    : args.provisionTimeout,
            "penc"  : args.provisionEncKey,
            "pauth" : args.provisionAuthKey,
            'pid'   : args.provisionUid,
            'aidt'  : args.authenticatorUidType,
            'aid'   : args.authenticatorUid,
            'nidt'  : args.nodeUidType,
            'nid'   : args.nodeUid,
        }))

    @staticmethod
    def fromDict(params : dict):

        return ProvisioningSettings(json.dumps(params))

    @staticmethod
    def isProvisionMethodSecure(method) -> bool:
        # The 'SECURED' method itself is also just the feature flag that is
        # shared between all the secured provisioning methods.
        return (PROV_METHOD[method] & PROV_METHOD['SECURED'])

    def __init__(self, params: str) -> None:

        cfg = json.loads(params)

        method_present = bool("pm" in cfg.keys() and cfg["pm"] is not None)
        self.method   = str(cfg["pm"]) if method_present else None

        retries_present = bool("pr" in cfg.keys() and cfg["pr"] is not None)
        self.retries  = int(cfg["pr"]) if retries_present else None

        timeout_present = bool("pt" in cfg.keys() and cfg["pt"] is not None)
        self.timeout  = int(cfg["pt"]) if timeout_present else None

        penc_present = bool("penc" in cfg.keys() and cfg["penc"] is not None)
        self.enc_key  = str(cfg["penc"]) if penc_present else None

        pauth_present = bool("pauth" in cfg.keys() and cfg["pauth"] is not None)
        self.auth_key = str(cfg["pauth"]) if pauth_present else None

        prov_uid_present = bool("pid" in cfg.keys() and cfg["pid"] is not None)
        self.prov_uid      = str(cfg["pid"]) if prov_uid_present else None

        auth_uid_type_present = bool("aidt" in cfg.keys() and cfg["aidt"] is not None)
        self.auth_uid_type = int(cfg["aidt"]) if auth_uid_type_present else None

        auth_uid_present = bool("aid" in cfg.keys() and cfg["aid"] is not None)
        self.auth_uid      = str(cfg["aid"]).replace("-","") \
                                if auth_uid_present else None

        node_uid_type_present = bool("nidt" in cfg.keys() and cfg["nidt"] is not None)
        self.node_uid_type = int(cfg["nidt"]) if node_uid_type_present else None

        node_uid_present = bool("nid" in cfg.keys() and cfg["nid"] is not None)
        self.node_uid = str(cfg["nid"]).replace("-","") \
                            if node_uid_present else None

    def validate(self) -> ProvisioningUIDType:

        if (self.method is not None and self.method not in PROV_METHOD.keys()):

            raise ValueError(f"Invalid provision method: {self.method}")

        method = PROV_METHOD[self.method] if self.method is not None else None

        method_euid = bool(method == PROV_METHOD['EUID']
                              or method == PROV_METHOD['EUID_KEY_MGMT']
                      ) if method is not None else False

        if (self.retries is not None):

            if (self.method is None):

                raise ValueError(
                    "Provision retries given without provision method"
                )

            elif (self.retries < 0 or self.retries > 255):

                raise ValueError(
                    "Provision retries must be positive integer between 0.255"
                )

        if (self.timeout is not None):

            if (self.method is None):

                raise ValueError(
                    "Provision timeout given without provision method.")

            elif (self.timeout < 0 or self.timeout > 65535):

                raise ValueError(
                    "Provision timeout must be positive integer between 0..65535")

        if (self.enc_key is not None):

            if (self.method is None or \
                not self.isProvisionMethodSecure(self.method)):

                raise ValueError(
                    "Provision encryption key allowed only with a secured method.")

            key_len = len(binascii.unhexlify(self.enc_key))

            if (key_len != SEC_KEY_LEN_BYTES):

                raise ValueError(f"Provision encryption key length {key_len}, "
                                  + f"must be {SEC_KEY_LEN_BYTES} bytes.")

        if (self.auth_key is not None):

            if (self.method is None or \
                not self.isProvisionMethodSecure(self.method)):

                raise ValueError(
                    "Provision authentication key allowed only with a secured method.")

            key_len = len(binascii.unhexlify(self.auth_key))

            if (key_len != SEC_KEY_LEN_BYTES):

                raise ValueError(
                    f"Provision authentication key length {key_len}, "
                     + f"must be {SEC_KEY_LEN_BYTES} bytes.")

        uid_type = ProvisioningUIDType.NONE
        if (self.prov_uid is not None):

            uid_len = len(binascii.unhexlify(self.prov_uid))

            if (method is None):
                raise ValueError("Provision UID given without provision method")

            elif (self.auth_uid_type is not None):
                raise ValueError("Either Authenticator UID Type or Provision UID can be given, not both.")

            elif (self.auth_uid is not None):
                raise ValueError("Either Authenticator UID or Provision UID can be given, not both.")

            elif (self.node_uid_type is not None):
                raise ValueError("Either Node UID Type or Provision UID can be given, not both.")

            elif (self.node_uid is not None):
                raise ValueError("Either Node UID or Provision UID can be given, not both.")

            elif (uid_len > PROV_UID_MAX_LEN):

                raise ValueError(f"Provision UID length: {uid_len}, "
                                  + f"max: {PROV_UID_MAX_LEN}")

            uid_type = ProvisioningUIDType.UID

        elif ( self.auth_uid_type or self.auth_uid or self.node_uid_type or self.node_uid):

            auth_uid_type_valid = False
            auth_uid_valid = False
            node_uid_type_valid = False
            node_uid_valid = False

            if (self.auth_uid_type is not None):
                auth_uid_type_valid = True

            if (self.auth_uid is not None):
                auid_len = len(binascii.unhexlify(self.auth_uid))
                if (auid_len != PROV_AUTH_UID_LEN):
                    raise ValueError(
                        f"Invalid authenticator UID length {auid_len} "
                        + f"expected {PROV_AUTH_UID_LEN}"
                    )
                auth_uid_valid = True

            if (self.node_uid_type is not None):
                node_uid_type_valid = True

            if (self.node_uid is not None):
                nuid_len = len(binascii.unhexlify(self.node_uid))
                if (nuid_len != PROV_EUID_LEN):
                    raise ValueError(
                        f"Invalid node UID length {nuid_len} "
                        + f"expected {PROV_EUID_LEN}"
                    )
                node_uid_valid = True

            if (not (auth_uid_type_valid and auth_uid_valid and node_uid_type_valid and node_uid_valid)):
                raise ValueError("Authentication UID Type, Authentication UID, Node UID Type and Node UID must either be all given or then none of them.")

            if (method is None or method_euid == False):
                raise ValueError("Authentication UID Type, Authentication UID, Node UID Type and Node UID allowed only when "
                                 + "provision method is extended UID.")

            uid_type = ProvisioningUIDType.EUID

        return uid_type

    def to_bytes(self) -> bytes:

        uid_type = self.validate()

        output = bytes()

        output += PROV_METHOD[self.method].to_bytes(1) \
                if self.method is not None else (0xff).to_bytes(1)
        output += self.retries.to_bytes(1) \
                if self.retries is not None else (0xff).to_bytes(1)
        output += self.timeout.to_bytes(2, byteorder='little') \
                if self.timeout is not None else (0xffff).to_bytes(2)
        output += binascii.unhexlify(self.enc_key) \
                if self.enc_key is not None \
                else bytes([0xff for i in range(SEC_KEY_LEN_BYTES)])
        output += binascii.unhexlify(self.auth_key) \
                if self.auth_key is not None \
                else bytes([0xff for i in range(SEC_KEY_LEN_BYTES)])

        if (uid_type == ProvisioningUIDType.UID):
            uid_value = self.prov_uid
        elif (uid_type == ProvisioningUIDType.EUID):
            uid_value = f"{self.auth_uid_type:02x}" + self.auth_uid + f"{self.node_uid_type:02x}" + self.node_uid
        else:
            uid_value = None

        # To have fixed width field, we pad the hex string before
        # converting that to byte array.
        output += binascii.unhexlify(uid_value.ljust(PROV_UID_MAX_LEN*2,'f')) \
                if uid_value is not None \
                else bytes([0xff for i in range(PROV_UID_MAX_LEN)])
        output += len(binascii.unhexlify(uid_value)).to_bytes(1) \
                if uid_value is not None else (0xff).to_bytes(1)

        return output


@dataclass
class ActionSettings:
    dualmcu_autostart: int
    preserve_data: int

    @staticmethod
    def loadArguments(parser : argparse.ArgumentParser) -> None:

        parser.add_argument(
            '-s', '--startDualMCU', default = None, required = False,
            type = int,
            help='Autostart dualmcu_app'
        )
        parser.add_argument(
            '-pd', '--preserveData', default = False, required = False,
            type = int,
            help='By default, application persistent area will be wiped clean '
                 + 'after first boot. Setting this to 1 means the data is '
                 + 'preserved for subsequent boots. '
                 + 'WARNING: Insecure. Use only on debugging and testing.'
        )

    @staticmethod
    def fromArgs(args : argparse.Namespace):

        return ActionSettings.fromDict(dict({
            "s"    : args.startDualMCU,
            "pd"    : args.preserveData,
        }))

    def fromDict(params : dict):

        return ActionSettings(json.dumps(params))

    def __init__(self, params: str) -> None:

        cfg = json.loads(params)

        self.dualmcu_autostart = int(cfg["s"]) if cfg["s"] is not None else None
        self.preserve_data = int(cfg["pd"]) if cfg["pd"] is not None else None

    def to_bytes(self) -> bytes:

        output = bytes()

        output += self.dualmcu_autostart.to_bytes(1) \
                    if self.dualmcu_autostart is not None \
                    else (0xff).to_bytes(1)
        output += self.preserve_data.to_bytes(1) \
                    if self.preserve_data is not None \
                    else (0xff).to_bytes(1)
        output += (0xffff).to_bytes(2) # Padding

        return output

def get_provision_method(args):

    if args.provisionMethod is not None:

        return args.provisionMethod

    else:

        return None

#
# Global variables
#
g_config = AppSetupConfig(None, None)

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
    parser.add_argument('-x', '--extraHexFiles',
                        default = None,
                        help='Extra hex files to be merged',
                        required=False)

    NodeSettings.loadArguments(parser)
    NetworkSettings.loadArguments(parser)
    ProvisioningSettings.loadArguments(parser)
    ActionSettings.loadArguments(parser)

    args = parser.parse_args()

    g_config.provisioning_method = get_provision_method(args)

    g_config.device = args.device

    node = NodeSettings.fromArgs(args)

    app_persistent_address = app_persistent_area_addresses[args.device]

    # Load hex file into memory
    ih = IntelHex(args.hexfile)

    # Fill whole persistent area with 0xFF (virtually the same as erasing it)
    for offset in range(APP_PERSISTENT_LEN):
        ih[app_persistent_address + offset] = 0xFF

    setup_data = bytes(
        (APP_PERSISTENT_MAGIC).to_bytes(4, byteorder='little')
        + Header.to_bytes()
        + node.to_bytes()
        + NetworkSettings.fromArgs(args).to_bytes()
        + ProvisioningSettings.fromArgs(args).to_bytes()
        + ActionSettings.fromArgs(args).to_bytes()
    )

    if (len(setup_data) != IMAGE_SIZE):
        raise ValueError(
            f"Data size mismatch, expected {IMAGE_SIZE}, "
             + f"got: {len(setup_data)}")

    # Copy the config to hex file
    for offset in range(IMAGE_SIZE):
        ih[app_persistent_address + offset] = setup_data[offset]

    if args.extraHexFiles:
        ehfs = args.extraHexFiles.split(',')
        for ehf in ehfs:
            ehfih = IntelHex(ehf)
            ih.merge(ehfih, overlap='replace')

    if node.node_address is not None:
        ih.write_hex_file(args.hexfile.rsplit('.',1)[0]+"_with_params_{}.hex".format(node.node_address))
    else:
        ih.write_hex_file(args.hexfile.rsplit('.',1)[0]+"_with_params.hex")
