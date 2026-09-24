package com.sensorv26.companion

import android.Manifest
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.dynamicLightColorScheme
import com.sensorv26.companion.ui.AppRoot
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue

class MainActivity : ComponentActivity() {

    private val vm: AppViewModel by viewModels()
    private var nfcAdapter: NfcAdapter? = null

    private var nfcAvailable by mutableStateOf(false)

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        nfcAdapter = NfcAdapter.getDefaultAdapter(this)
        requestBlePermissions()

        setContent {
            val colors = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                dynamicLightColorScheme(this) else MaterialTheme.colorScheme
            MaterialTheme(colorScheme = colors) {
                AppRoot(vm = vm, nfcAvailable = nfcAvailable)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        nfcAvailable = nfcAdapter?.isEnabled == true
        enableNfcReaderMode()
    }

    override fun onPause() {
        super.onPause()
        nfcAdapter?.disableReaderMode(this)
    }

    private fun enableNfcReaderMode() {
        val adapter = nfcAdapter ?: return
        // FLAG_READER_SKIP_NDEF_CHECK: the platform otherwise runs its own NDEF
        // probe on every newly-detected tag before handing it to this callback -
        // harmless for the mesh tag (genuinely NDEF, and NfcNdef.read() does its
        // own explicit Ndef.get()/connect() regardless of this flag), but for the
        // SE050 (not an NDEF applet at all) that platform-level probe was
        // preventing the tag from ever reaching onTagDiscovered - no callback, no
        // error, nothing in logcat, since the NDEF check runs inside NfcService
        // before our code is ever invoked. Found 2026-09-22 debugging exactly
        // that symptom against a real SE050C2 already confirmed working NFC with
        // a generic reader app.
        val flags = NfcAdapter.FLAG_READER_NFC_A or
                NfcAdapter.FLAG_READER_NFC_B or
                NfcAdapter.FLAG_READER_NFC_F or
                NfcAdapter.FLAG_READER_NFC_V or
                NfcAdapter.FLAG_READER_SKIP_NDEF_CHECK
        val extras = Bundle().apply {
            putInt(NfcAdapter.EXTRA_READER_PRESENCE_CHECK_DELAY, 250)
        }
        adapter.enableReaderMode(this, { tag: Tag -> vm.onTagDiscovered(tag) }, flags, extras)
    }

    private fun requestBlePermissions() {
        val perms = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_ADVERTISE,
                Manifest.permission.BLUETOOTH_CONNECT,
            )
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        permissionLauncher.launch(perms)
    }
}
