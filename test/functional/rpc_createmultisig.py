#!/usr/bin/env python3
# Copyright (c) 2015-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test multisig RPCs"""
import decimal
import itertools
import json
import os

from test_framework.address import address_to_scriptpubkey
from test_framework.blocktools import COINBASE_MATURITY
from test_framework.authproxy import JSONRPCException
from test_framework.descriptors import descsum_create, drop_origins
from test_framework.key import ECPubKey, ECKey
from test_framework.test_framework import MytherraTestFramework
from test_framework.util import (
    assert_raises_rpc_error,
    assert_equal,
)
from test_framework.wallet_util import bytes_to_wif
from test_framework.wallet import (
    MiniWallet,
    getnewdestination,
)

class RpcCreateMultiSigTest(MytherraTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.supports_cli = False

    def get_keys(self):
        self.pub = []
        self.priv = []
        node0, node1, node2 = self.nodes
        for _ in range(self.nkeys):
            k = ECKey()
            k.generate()
            self.pub.append(k.get_pubkey().get_bytes().hex())
            self.priv.append(bytes_to_wif(k.get_bytes(), k.is_compressed))
        if self.is_bdb_compiled():
            self.final = node2.getnewaddress()
        else:
            self.final = getnewdestination('bech32')[2]

    def run_test(self):
        node0, node1, node2 = self.nodes
        self.wallet = MiniWallet(test_node=node0)

        if self.is_bdb_compiled():
            self.import_deterministic_coinbase_privkeys()
            self.check_addmultisigaddress_errors()

        self.log.info('Generating blocks ...')
        self.generate(self.wallet, 149)

        self.moved = 0
        for self.nkeys in [3, 5]:
            for self.nsigs in [2, 3]:
                for self.output_type in ["bech32", "p2sh-segwit", "legacy"]:
                    self.get_keys()
                    self.do_multisig()
        if self.is_bdb_compiled():
            self.checkbalances()

        # Test mixed compressed and uncompressed pubkeys
        self.log.info('Mixed compressed and uncompressed multisigs are not allowed')
        pk0, pk1, pk2 = [getnewdestination('bech32')[0].hex() for _ in range(3)]

        # decompress pk2
        pk_obj = ECPubKey()
        pk_obj.set(bytes.fromhex(pk2))
        pk_obj.compressed = False
        pk2 = pk_obj.get_bytes().hex()

        if self.is_bdb_compiled():
            node0.createwallet(wallet_name='wmulti0', disable_private_keys=True)
            wmulti0 = node0.get_wallet_rpc('wmulti0')

        # Check all permutations of keys because order matters apparently
        for keys in itertools.permutations([pk0, pk1, pk2]):
            # Results should be the same as this legacy one
            legacy_addr = node0.createmultisig(2, keys, 'legacy')['address']

            if self.is_bdb_compiled():
                result = wmulti0.addmultisigaddress(2, keys, '', 'legacy')
                assert_equal(legacy_addr, result['address'])
                assert 'warnings' not in result

            # Generate addresses with the segwit types. These should all make legacy addresses
            err_msg = ["Unable to make chosen address type, please ensure no uncompressed public keys are present."]

            for addr_type in ['bech32', 'p2sh-segwit']:
                result = self.nodes[0].createmultisig(nrequired=2, keys=keys, address_type=addr_type)
                assert_equal(legacy_addr, result['address'])
                assert_equal(result['warnings'], err_msg)

                if self.is_bdb_compiled():
                    result = wmulti0.addmultisigaddress(nrequired=2, keys=keys, address_type=addr_type)
                    assert_equal(legacy_addr, result['address'])
                    assert_equal(result['warnings'], err_msg)

        self.log.info('Testing sortedmulti descriptors with BIP 67 test vectors')
        with open(os.path.join(os.path.dirname(os.path.realpath(__file__)), 'data/rpc_bip67.json'), encoding='utf-8') as f:
            vectors = json.load(f)

        for t in vectors:
            key_str = ','.join(t['keys'])
            desc = descsum_create('sh(sortedmulti(2,{}))'.format(key_str))
            assert_equal(self.nodes[0].deriveaddresses(desc)[0], t['address'])
            sorted_key_str = ','.join(t['sorted_keys'])
            sorted_key_desc = descsum_create('sh(multi(2,{}))'.format(sorted_key_str))
            assert_equal(self.nodes[0].deriveaddresses(sorted_key_desc)[0], t['address'])

        # Check that bech32m is currently not allowed
        assert_raises_rpc_error(-5, "createmultisig cannot create bech32m multisig addresses", self.nodes[0].createmultisig, 2, self.pub, "bech32m")

    def check_addmultisigaddress_errors(self):
        if self.options.descriptors:
            return
        self.log.info('Check that addmultisigaddress fails when the private keys are missing')
        addresses = [self.nodes[1].getnewaddress(address_type='legacy') for _ in range(2)]
        assert_raises_rpc_error(-5, 'no full public key for address', lambda: self.nodes[0].addmultisigaddress(nrequired=1, keys=addresses))
        for a in addresses:
            # Importing all addresses should not change the result
            self.nodes[0].importaddress(a)
        assert_raises_rpc_error(-5, 'no full public key for address', lambda: self.nodes[0].addmultisigaddress(nrequired=1, keys=addresses))

        # Bech32m address type is disallowed for legacy wallets
        pubs = [self.nodes[1].getaddressinfo(addr)["pubkey"] for addr in addresses]
        assert_raises_rpc_error(-5, "Bech32m multisig addresses cannot be created with legacy wallets", self.nodes[0].addmultisigaddress, 2, pubs, "", "bech32m")

    def checkbalances(self):
        node0, node1, node2 = self.nodes
        self.generate(node0, COINBASE_MATURITY)

        bal0 = node0.getbalance()
        bal1 = node1.getbalance()
        bal2 = node2.getbalance()
        balw = self.wallet.get_balance()

        height = node0.getblockchaininfo()["blocks"]
        assert 150 < height < 350
        total = 149 * 50 + (height - 149 - 100) * 25
        assert bal1 == 0
        assert bal2 == self.moved
        assert_equal(bal0 + bal1 + bal2 + balw, total)

    def do_multisig(self):
        node0, node1, node2 = self.nodes

        if self.is_bdb_compiled():
            if 'wmulti' not in node1.listwallets():
                try:
                    node1.loadwallet('wmulti')
                except JSONRPCException as e:
                    path = os.path.join(self.options.tmpdir, "node1", "regtest", "wallets", "wmulti")
                    if e.error['code'] == -18 and "Wallet file verification failed. Failed to load database path '{}'. Path does not exist.".format(path) in e.error['message']:
                        node1.createwallet(wallet_name='wmulti', disable_private_keys=True)
                    else:
                        raise
            wmulti = node1.get_wallet_rpc('wmulti')

        # Construct the expected descriptor
        desc = 'multi({},{})'.format(self.nsigs, ','.join(self.pub))
        if self.output_type == 'legacy':
            desc = 'sh({})'.format(desc)
        elif self.output_type == 'p2sh-segwit':
            desc = 'sh(wsh({}))'.format(desc)
        elif self.output_type == 'bech32':
            desc = 'wsh({})'.format(desc)
        desc = descsum_create(desc)

        msig = node2.createmultisig(self.nsigs, self.pub, self.output_type)
        assert 'warnings' not in msig
        madd = msig["address"]
        mredeem = msig["redeemScript"]
        assert_equal(desc, msig['descriptor'])
        if self.output_type == 'bech32':
            assert madd[0:4] == "bcrt"  # actually a bech32 address

        if self.is_bdb_compiled():
            # compare against addmultisigaddress
            msigw = wmulti.addmultisigaddress(self.nsigs, self.pub, None, self.output_type)
            maddw = msigw["address"]
            mredeemw = msigw["redeemScript"]
            assert_equal(desc, drop_origins(msigw['descriptor']))
            # addmultisigiaddress and createmultisig work the same
            assert maddw == madd
            assert mredeemw == mredeem
            wmulti.unloadwallet()

        spk = address_to_scriptpubkey(madd)
        txid, _ = self.wallet.send_to(from_node=self.nodes[0], scriptPubKey=spk, amount=1300)
        tx = node0.getrawtransaction(txid, True)
        vout = [v["n"] for v in tx["vout"] if madd == v["scriptPubKey"]["address"]]
        assert len(vout) == 1
        vout = vout[0]
        scriptPubKey = tx["vout"][vout]["scriptPubKey"]["hex"]
        value = tx["vout"][vout]["value"]
        prevtxs = [{"txid": txid, "vout": vout, "scriptPubKey": scriptPubKey, "redeemScript": mredeem, "amount": value}]

        self.generate(node0, 1)

        outval = value - decimal.Decimal("0.00001000")
        rawtx = node2.createrawtransaction([{"txid": txid, "vout": vout}], [{self.final: outval}])

        prevtx_err = dict(prevtxs[0])
        del prevtx_err["redeemScript"]

        assert_raises_rpc_error(-8, "Missing redeemScript/witnessScript", node2.signrawtransactionwithkey, rawtx, self.priv[0:self.nsigs-1], [prevtx_err])

        # if witnessScript specified, all ok
        prevtx_err["witnessScript"] = prevtxs[0]["redeemScript"]
        node2.signrawtransactionwithkey(rawtx, self.priv[0:self.nsigs-1], [prevtx_err])

        # both specified, also ok
        prevtx_err["redeemScript"] = prevtxs[0]["redeemScript"]
        node2.signrawtransactionwithkey(rawtx, self.priv[0:self.nsigs-1], [prevtx_err])

        # redeemScript mismatch to witnessScript
        prevtx_err["redeemScript"] = "6a" # OP_RETURN
        assert_raises_rpc_error(-8, "redeemScript does not correspond to witnessScript", node2.signrawtransactionwithkey, rawtx, self.priv[0:self.nsigs-1], [prevtx_err])

        # redeemScript does not match scriptPubKey
        del prevtx_err["witnessScript"]
        assert_raises_rpc_error(-8, "redeemScript/witnessScript does not match scriptPubKey", node2.signrawtransactionwithkey, rawtx, self.priv[0:self.nsigs-1], [prevtx_err])

        # witnessScript does not match scriptPubKey
        prevtx_err["witnessScript"] = prevtx_err["redeemScript"]
        del prevtx_err["redeemScript"]
        assert_raises_rpc_error(-8, "redeemScript/witnessScript does not match scriptPubKey", node2.signrawtransactionwithkey, rawtx, self.priv[0:self.nsigs-1], [prevtx_err])

        rawtx2 = node2.signrawtransactionwithkey(rawtx, self.priv[0:self.nsigs - 1], prevtxs)
        rawtx3 = node2.signrawtransactionwithkey(rawtx2["hex"], [self.priv[-1]], prevtxs)

        self.moved += outval
        tx = node0.sendrawtransaction(rawtx3["hex"], 0)
        blk = self.generate(node0, 1)[0]
        assert tx in node0.getblock(blk)["tx"]

        txinfo = node0.getrawtransaction(tx, True, blk)
        self.log.info("n/m=%d/%d %s size=%d vsize=%d weight=%d" % (self.nsigs, self.nkeys, self.output_type, txinfo["size"], txinfo["vsize"], txinfo["weight"]))


if __name__ == '__main__':
    RpcCreateMultiSigTest().main()
