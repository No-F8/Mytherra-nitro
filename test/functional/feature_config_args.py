#!/usr/bin/env python3
# Copyright (c) 2017-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test various command line arguments and configuration file parameters."""

import os
import time

from test_framework.test_framework import MytherraTestFramework
from test_framework import util


class ConfArgsTest(MytherraTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.supports_cli = False
        self.wallet_names = []
        self.disable_autoconnect = False

    def test_config_file_parser(self):
        self.log.info('Test config file parser')
        self.stop_node(0)

        # Check that startup fails if conf= is set in mytherra.conf or in an included conf file
        bad_conf_file_path = os.path.join(self.options.tmpdir, 'node0', 'mytherra_bad.conf')
        util.write_config(bad_conf_file_path, n=0, chain='', extra_config=f'conf=some.conf\n')
        conf_in_config_file_err = 'Error: Error reading configuration file: conf cannot be set in the configuration file; use includeconf= if you want to include additional config files'
        self.nodes[0].assert_start_raises_init_error(
            extra_args=[f'-conf={bad_conf_file_path}'],
            expected_msg=conf_in_config_file_err,
        )
        inc_conf_file_path = os.path.join(self.nodes[0].datadir, 'include.conf')
        with open(os.path.join(self.nodes[0].datadir, 'mytherra.conf'), 'a', encoding='utf-8') as conf:
            conf.write(f'includeconf={inc_conf_file_path}\n')
        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('conf=some.conf\n')
        self.nodes[0].assert_start_raises_init_error(
            expected_msg=conf_in_config_file_err,
        )

        self.nodes[0].assert_start_raises_init_error(
            expected_msg='Error: Error parsing command line arguments: Invalid parameter -dash_cli=1',
            extra_args=['-dash_cli=1'],
        )
        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('dash_conf=1\n')

        with self.nodes[0].assert_debug_log(expected_msgs=['Ignoring unknown configuration value dash_conf']):
            self.start_node(0)
        self.stop_node(0)

        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('reindex=1\n')

        with self.nodes[0].assert_debug_log(expected_msgs=['Warning: reindex=1 is set in the configuration file, which will significantly slow down startup. Consider removing or commenting out this option for better performance, unless there is currently a condition which makes rebuilding the indexes necessary']):
            self.start_node(0)
        self.stop_node(0)

        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('-dash=1\n')
        self.nodes[0].assert_start_raises_init_error(expected_msg='Error: Error reading configuration file: parse error on line 1: -dash=1, options in configuration file must be specified without leading -')

        if self.is_wallet_compiled():
            with open(inc_conf_file_path, 'w', encoding='utf8') as conf:
                conf.write("wallet=foo\n")
            self.nodes[0].assert_start_raises_init_error(expected_msg=f'Error: Config setting for -wallet only applied on {self.chain} network when in [{self.chain}] section.')

        main_conf_file_path = os.path.join(self.options.tmpdir, 'node0', 'mytherra_main.conf')
        util.write_config(main_conf_file_path, n=0, chain='', extra_config=f'includeconf={inc_conf_file_path}\n')
        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('acceptnonstdtxn=1\n')
        self.nodes[0].assert_start_raises_init_error(extra_args=[f"-conf={main_conf_file_path}"], expected_msg='Error: acceptnonstdtxn is not currently supported for main chain')

        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('nono\n')
        self.nodes[0].assert_start_raises_init_error(expected_msg='Error: Error reading configuration file: parse error on line 1: nono, if you intended to specify a negated option, use nono=1 instead')

        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('server=1\nrpcuser=someuser\nrpcpassword=some#pass')
        self.nodes[0].assert_start_raises_init_error(expected_msg='Error: Error reading configuration file: parse error on line 3, using # in rpcpassword can be ambiguous and should be avoided')

        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('server=1\nrpcuser=someuser\nmain.rpcpassword=some#pass')
        self.nodes[0].assert_start_raises_init_error(expected_msg='Error: Error reading configuration file: parse error on line 3, using # in rpcpassword can be ambiguous and should be avoided')

        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('server=1\nrpcuser=someuser\n[main]\nrpcpassword=some#pass')
        self.nodes[0].assert_start_raises_init_error(expected_msg='Error: Error reading configuration file: parse error on line 4, using # in rpcpassword can be ambiguous and should be avoided')

        inc_conf_file2_path = os.path.join(self.nodes[0].datadir, 'include2.conf')
        with open(os.path.join(self.nodes[0].datadir, 'mytherra.conf'), 'a', encoding='utf-8') as conf:
            conf.write(f'includeconf={inc_conf_file2_path}\n')

        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('testnot.datadir=1\n')
        with open(inc_conf_file2_path, 'w', encoding='utf-8') as conf:
            conf.write('[testnet]\n')
        self.restart_node(0)
        self.nodes[0].stop_node(expected_stderr=f'Warning: {inc_conf_file_path}:1 Section [testnot] is not recognized.{os.linesep}{inc_conf_file2_path}:1 Section [testnet] is not recognized.')

        with open(inc_conf_file_path, 'w', encoding='utf-8') as conf:
            conf.write('')  # clear
        with open(inc_conf_file2_path, 'w', encoding='utf-8') as conf:
            conf.write('')  # clear

    def test_invalid_command_line_options(self):
        self.nodes[0].assert_start_raises_init_error(
            expected_msg='Error: Error parsing command line arguments: Can not set -proxy with no value. Please specify value with -proxy=value.',
            extra_args=['-proxy'],
        )

    def test_log_buffer(self):
        self.stop_node(0)
        with self.nodes[0].assert_debug_log(expected_msgs=['Warning: parsed potentially confusing double-negative -connect=0\n']):
            self.start_node(0, extra_args=['-noconnect=0'])

    def test_args_log(self):
        self.stop_node(0)
        self.log.info('Test config args logging')
        with self.nodes[0].assert_debug_log(
                expected_msgs=[
                    'Command-line arg: addnode="some.node"',
                    'Command-line arg: rpcauth=****',
                    'Command-line arg: rpcpassword=****',
                    'Command-line arg: rpcuser=****',
                    'Command-line arg: torpassword=****',
                    f'Config file arg: {self.chain}="1"',
                    f'Config file arg: [{self.chain}] server="1"',
                ],
                unexpected_msgs=[
                    'alice:f7efda5c189b999524f151318c0c86$d5b51b3beffbc0',
                    'secret-rpcuser',
                    'secret-torpassword',
                    'Command-line arg: rpcbind=****',
                    'Command-line arg: rpcallowip=****',
                ]):
            self.start_node(0, extra_args=[
                '-addnode=some.node',
                '-rpcauth=alice:f7efda5c189b999524f151318c0c86$d5b51b3beffbc0',
                '-rpcbind=127.1.1.1',
                '-rpcbind=127.0.0.1',
                "-rpcallowip=127.0.0.1",
                '-rpcpassword=',
                '-rpcuser=secret-rpcuser',
                '-torpassword=secret-torpassword',
            ])

    def test_networkactive(self):
        self.log.info('Test -networkactive option')
        self.stop_node(0)
        with self.nodes[0].assert_debug_log(expected_msgs=['SetNetworkActive: true\n']):
            self.start_node(0)

        self.stop_node(0)
        with self.nodes[0].assert_debug_log(expected_msgs=['SetNetworkActive: true\n']):
            self.start_node(0, extra_args=['-networkactive'])

        self.stop_node(0)
        with self.nodes[0].assert_debug_log(expected_msgs=['SetNetworkActive: true\n']):
            self.start_node(0, extra_args=['-networkactive=1'])

        self.stop_node(0)
        with self.nodes[0].assert_debug_log(expected_msgs=['SetNetworkActive: false\n']):
            self.start_node(0, extra_args=['-networkactive=0'])

        self.stop_node(0)
        with self.nodes[0].assert_debug_log(expected_msgs=['SetNetworkActive: false\n']):
            self.start_node(0, extra_args=['-nonetworkactive'])

        self.stop_node(0)
        with self.nodes[0].assert_debug_log(expected_msgs=['SetNetworkActive: false\n']):
            self.start_node(0, extra_args=['-nonetworkactive=1'])

    def test_seed_peers(self):
        self.log.info('Test seed peers')
        default_data_dir = self.nodes[0].datadir
        # Only regtest has no fixed seeds. To avoid connections to random
        # nodes, regtest is the only network where it is safe to enable
        # -fixedseeds in tests
        util.assert_equal(self.nodes[0].getblockchaininfo()['chain'],'regtest')
        self.stop_node(0)

        # No peers.dat exists and -dnsseed=1
        # We expect the node will use DNS Seeds, but Regtest mode does not have
        # any valid DNS seeds. So after 60 seconds, the node should fallback to
        # fixed seeds
        assert not os.path.exists(os.path.join(default_data_dir, "peers.dat"))
        start = int(time.time())
        with self.nodes[0].assert_debug_log(
                expected_msgs=[
                    "Loaded 0 addresses from peers.dat",
                    "0 addresses found from DNS seeds",
                    "opencon thread start",  # Ensure ThreadOpenConnections::start time is properly set
                ],
                timeout=10,
        ):
            self.start_node(0, extra_args=['-dnsseed=1', '-fixedseeds=1', f'-mocktime={start}'])
        with self.nodes[0].assert_debug_log(expected_msgs=[
                "Adding fixed seeds as 60 seconds have passed and addrman is empty",
        ]):
            self.nodes[0].setmocktime(start + 65)
        self.stop_node(0)

        # No peers.dat exists and -dnsseed=0
        # We expect the node will fallback immediately to fixed seeds
        assert not os.path.exists(os.path.join(default_data_dir, "peers.dat"))
        start = time.time()
        with self.nodes[0].assert_debug_log(expected_msgs=[
                "Loaded 0 addresses from peers.dat",
                "DNS seeding disabled",
                "Adding fixed seeds as -dnsseed=0 (or IPv4/IPv6 connections are disabled via -onlynet), -addnode is not provided and all -seednode(s) attempted\n",
        ]):
            self.start_node(0, extra_args=['-dnsseed=0', '-fixedseeds=1'])
        assert time.time() - start < 60
        self.stop_node(0)
        self.nodes[0].assert_start_raises_init_error(['-dnsseed=1', '-onlynet=i2p', '-i2psam=127.0.0.1:7656'], "Error: Incompatible options: -dnsseed=1 was explicitly specified, but -onlynet forbids connections to IPv4/IPv6")

        # No peers.dat exists and dns seeds are disabled.
        # We expect the node will not add fixed seeds when explicitly disabled.
        assert not os.path.exists(os.path.join(default_data_dir, "peers.dat"))
        start = time.time()
        with self.nodes[0].assert_debug_log(expected_msgs=[
                "Loaded 0 addresses from peers.dat",
                "DNS seeding disabled",
                "Fixed seeds are disabled",
        ]):
            self.start_node(0, extra_args=['-dnsseed=0', '-fixedseeds=0'])
        assert time.time() - start < 60
        self.stop_node(0)

        # No peers.dat exists and -dnsseed=0, but a -addnode is provided
        # We expect the node will allow 60 seconds prior to using fixed seeds
        assert not os.path.exists(os.path.join(default_data_dir, "peers.dat"))
        start = int(time.time())
        with self.nodes[0].assert_debug_log(
                expected_msgs=[
                    "Loaded 0 addresses from peers.dat",
                    "DNS seeding disabled",
                    "opencon thread start",  # Ensure ThreadOpenConnections::start time is properly set
                ],
                timeout=10,
        ):
            self.start_node(0, extra_args=['-dnsseed=0', '-fixedseeds=1', '-addnode=fakenodeaddr', f'-mocktime={start}'])
        with self.nodes[0].assert_debug_log(expected_msgs=[
                "Adding fixed seeds as 60 seconds have passed and addrman is empty",
        ]):
            self.nodes[0].setmocktime(start + 65)

    def test_connect_with_seednode(self):
        self.log.info('Test -connect with -seednode')
        seednode_ignored = ['-seednode is ignored when -connect is used\n']
        dnsseed_ignored = ['-dnsseed is ignored when -connect is used and -proxy is specified\n']
        addcon_thread_started = ['addcon thread start\n']
        self.stop_node(0)

        # When -connect is supplied, expanding addrman via getaddr calls to ADDR_FETCH(-seednode)
        # nodes is irrelevant and -seednode is ignored.
        with self.nodes[0].assert_debug_log(expected_msgs=seednode_ignored):
            self.start_node(0, extra_args=['-connect=fakeaddress1', '-seednode=fakeaddress2'])

        # With -proxy, an ADDR_FETCH connection is made to a peer that the dns seed resolves to.
        # ADDR_FETCH connections are not used when -connect is used.
        with self.nodes[0].assert_debug_log(expected_msgs=dnsseed_ignored):
            self.restart_node(0, extra_args=['-connect=fakeaddress1', '-dnsseed=1', '-proxy=1.2.3.4'])

        # If the user did not disable -dnsseed, but it was soft-disabled because they provided -connect,
        # they shouldn't see a warning about -dnsseed being ignored.
        with self.nodes[0].assert_debug_log(expected_msgs=addcon_thread_started,
                unexpected_msgs=dnsseed_ignored):
            self.restart_node(0, extra_args=['-connect=fakeaddress1', '-proxy=1.2.3.4'])

        # We have to supply expected_msgs as it's a required argument
        # The expected_msg must be something we are confident will be logged after the unexpected_msg
        # These cases test for -connect being supplied but only to disable it
        for connect_arg in ['-connect=0', '-noconnect']:
            with self.nodes[0].assert_debug_log(expected_msgs=addcon_thread_started,
                    unexpected_msgs=seednode_ignored):
                self.restart_node(0, extra_args=[connect_arg, '-seednode=fakeaddress2'])

    def run_test(self):
        self.test_log_buffer()
        self.test_args_log()
        self.test_seed_peers()
        self.test_networkactive()
        self.test_connect_with_seednode()

        self.test_config_file_parser()
        self.test_invalid_command_line_options()

        # Remove the -datadir argument so it doesn't override the config file
        self.nodes[0].args = [arg for arg in self.nodes[0].args if not arg.startswith("-datadir")]

        default_data_dir = self.nodes[0].datadir
        new_data_dir = os.path.join(default_data_dir, 'newdatadir')
        new_data_dir_2 = os.path.join(default_data_dir, 'newdatadir2')

        # Check that using -datadir argument on non-existent directory fails
        self.nodes[0].datadir = new_data_dir
        self.nodes[0].assert_start_raises_init_error([f'-datadir={new_data_dir}'], f'Error: Specified data directory "{new_data_dir}" does not exist.')

        # Check that using non-existent datadir in conf file fails
        conf_file = os.path.join(default_data_dir, "mytherra.conf")

        # datadir needs to be set before [chain] section
        with open(conf_file, encoding='utf8') as f:
            conf_file_contents = f.read()
        with open(conf_file, 'w', encoding='utf8') as f:
            f.write(f"datadir={new_data_dir}\n")
            f.write(conf_file_contents)

        self.nodes[0].assert_start_raises_init_error([f'-conf={conf_file}'], f'Error: Error reading configuration file: specified data directory "{new_data_dir}" does not exist.')

        # Check that an explicitly specified config file that cannot be opened fails
        none_existent_conf_file = os.path.join(default_data_dir, "none_existent_mytherra.conf")
        self.nodes[0].assert_start_raises_init_error(['-conf=' + none_existent_conf_file], 'Error: Error reading configuration file: specified config file "' + none_existent_conf_file + '" could not be opened.')

        # Create the directory and ensure the config file now works
        os.mkdir(new_data_dir)
        self.start_node(0, [f'-conf={conf_file}'])
        self.stop_node(0)
        assert os.path.exists(os.path.join(new_data_dir, self.chain, 'blocks'))

        # Ensure command line argument overrides datadir in conf
        os.mkdir(new_data_dir_2)
        self.nodes[0].datadir = new_data_dir_2
        self.start_node(0, [f'-datadir={new_data_dir_2}', f'-conf={conf_file}'])
        assert os.path.exists(os.path.join(new_data_dir_2, self.chain, 'blocks'))


if __name__ == '__main__':
    ConfArgsTest().main()
