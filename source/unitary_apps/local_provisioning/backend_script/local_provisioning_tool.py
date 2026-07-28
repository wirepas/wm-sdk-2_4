# Copyright 2021 Wirepas Ltd. All Rights Reserved.
#
# See file LICENSE.txt for full license details.
#
# This script is an example built on top of wirepas-mqtt-library wheel >= 1.1 to
# demonstrate the local provisioning feature.
#
# Once started, it connects to an mqtt broker and will do the following operation:
#    - It will configure any new connected sink without a network address to the new_config hardcoded bellow as an example
#    - It will wait for user input to enable or disable the local provisioning in the network
#
import argparse
import logging
import sys

try:
    from wirepas_mqtt_library import WirepasNetworkInterface, WirepasTLVAppConfigHelper
    import wirepas_mesh_messaging as wmm
except ModuleNotFoundError:
    print(
        "Please install Wirepas mqtt library wheel (>= 1.1): pip install wirepas-mqtt-library==1.1"
    )
    sys.exit(-1)

local_provisioning_status = False

# Hardcoded config as an example
new_config = {
    "node_address": 0x1000,
    "node_role": 17,  # Sink with Low Latency flag: 0x11
    "network_address": 123456,
    "network_channel": 9,
    # Example keys to illustrate the concept
    # fmt: off
    "cipher_key": bytes([0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88]),
    "authentication_key": bytes([0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88]),
    # fmt: on
    "started": True,
}

# Example of Pre-shared key and ID
# This PSK (Pre-Shared Key) is a shared secret between the device and the network, enabling mutual authentication.
# Therefore, the same values must be defined in the node when calling `Local_provisioning_init()`.
custom_psk_config = {
    # fmt: off
    "custom_psk_id": bytearray([0x12, 0x34, 0x56, 0x78]),
    "custom_psk": bytes([0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5])
    # fmt: on
}

# PSK ID should be transmitted as Little Endian, hence we reverse it.
# This allows to copy/paste the value in local_provisioning application (in unitary_app folder)
custom_psk_config["custom_psk_id"].reverse()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(fromfile_prefix_chars="@")
    parser.add_argument("--host", help="MQTT broker address")
    parser.add_argument("--port", type=int, default=8883, help="MQTT broker port")
    parser.add_argument("--username", help="MQTT broker username")
    parser.add_argument("--password", help="MQTT broker password")
    parser.add_argument(
        "--force",
        dest="force",
        action="store_true",
        help="Force configuration of the sink with the one provided in new_config.",
    )
    parser.add_argument(
        "--insecure",
        dest="insecure",
        action="store_true",
        help="MQTT use unsecured connection",
    )

    args = parser.parse_args()

    def enable_local_provisioning(enable, psk_id=None, psk=None):
        """
        Enable or disable local provisioning using the App Config.

        :param enable: Boolean to enable/ disable the Local Provisionning
        :param psk_id: The Key ID used as the pre-shared secret key id to secure data exchange
        :param psk: The Key used as the pre-shared secret key to secure data exchange

        """

        # Create the app config helper to build the data to be sent over the mesh network.
        app_config_helper = WirepasTLVAppConfigHelper(
            wni, network=new_config["network_address"]
        )

        # Send app config to the sink.
        res = app_config_helper.setup_local_provisioning(
            enable, psk_id, psk
        ).update_entries(override=True)

        if res:
            logging.debug("Local provisioning state updated successfully = %s" % res)
            return enable
        else:
            logging.debug(
                "Local provisioning state failed to update with error = %s" % res
            )
            return local_provisioning_status

    def check_config_parameters(config):
        """
        Check that the sink configuration is sufficient to perform local provisioning

        :param config: current sink config to be checked

        """
        # Check for mandatory parameters
        try:
            for parameter in [
                "are_keys_set",
                "network_address",
                "node_address",
                "network_channel",
            ]:
                _ = config[parameter]
        except KeyError as e:
            logging.error("{} of {}/{} is not configured yet".format(e, gw, sink))

    # Callback called when the app config is updated.
    def on_config_changed():
        if local_provisioning_status:
            enable_local_provisioning(local_provisioning_status)

    logging.basicConfig(
        format="%(levelname)s %(asctime)s %(message)s", level=logging.INFO
    )
    wni = WirepasNetworkInterface(
        args.host, args.port, args.username, args.password, insecure=args.insecure
    )

    logging.debug("Is force sink to new config activated: %s" % args.force)

    # Checking if the sink is configured correctly to start the local provisioning.
    try:
        for gw, sink, current_config in wni.get_sinks(network_address=None):
            logging.debug(current_config)

            if args.force == False:
                check_config_parameters(current_config)

                if new_config["node_address"] != current_config["node_address"]:
                    logging.critical(
                        """\tYour current sink configuration differs from the new configuration.
                                        Your new node address differs from your current sink node address.
                                        To force the configuration of your sink with the new configuration, use option --force"""
                    )
                    exit()

                if new_config["network_address"] != current_config["network_address"]:
                    logging.critical(
                        """\tYour current sink configuration differs from the new configuration.
                                        Your new network address differs from your current sink netwok address.
                                        To force the configuration of your sink with the new configuration, use option --force"""
                    )
                    exit()

                if new_config["network_channel"] != current_config["network_channel"]:
                    logging.critical(
                        """\tYour current sink configuration differs from the new configuration.
                                        Your new network channel id differs from your current sink netwok channel id.
                                        To force the configuration of your sink with the new configuration, use option --force"""
                    )
                    exit()

                if not current_config["are_keys_set"]:
                    logging.critical(
                        """\tYour current sink configuration is incomplete.
                                        You must set the Authentication and Encryption keys before running the local provisionning.
                                        To force the configuration of your sink with the new configuration, use option --force"""
                    )
                    exit()

            else:
                try:
                    logging.info(
                        "Configuring the sink with the parameters provided in new_config"
                    )
                    res = wni.set_sink_config(gw, sink, new_config)
                    if res != wmm.GatewayResultCode.GW_RES_OK:
                        logging.error(
                            "Cannot set new config to %s:%s res=%s"
                            % (gw, sink, res.res)
                        )
                except TimeoutError:
                    logging.error(
                        "Cannot set new config to %s:%s due to timeout" % (gw, sink)
                    )
    except TimeoutError:
        logging.error("Could not connect to MQTT broker. Timeout error.")

    try:
        wni.set_config_changed_cb(on_config_changed)
    except TimeoutError:
        logging.error("Could not connect to MQTT broker. Timeout error.")
        exit()

    while True:
        choice = input(
            "\n(E)nable local provisioning, (D)isable local provisioning, (Q)uit:\n"
        )
        if choice == "Q":
            break
        elif choice == "E":
            local_provisioning_status = False
            print(
                "Enabling local provisioning in network 0x%x"
                % new_config["network_address"]
            )
            local_provisioning_status = enable_local_provisioning(
                True,
                custom_psk_config["custom_psk_id"],
                custom_psk_config["custom_psk"],
            )
        elif choice == "D":
            local_provisioning_status = False
            print(
                "Disabling local provisioning in network 0x%x"
                % new_config["network_address"]
            )
            local_provisioning_status = enable_local_provisioning(False)
        else:
            print("Wrong choice E, D or Q")
