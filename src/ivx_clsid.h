#pragma once

// The class id of the SAPI5 engine object.
//
// Spelled once, here, because three separate things have to agree on it and a
// disagreement is invisible until a user picks a voice and hears nothing: the
// object's own __declspec(uuid), what DllRegisterServer writes under
// HKLM\Software\Classes\CLSID, and what every voice token names as its CLSID.
//
// It is specific to this product. Infovox 230 v2.2 has a wrapper of its own with
// a different id, and both can be installed on one machine.
#define IVX_TTSENGINE_CLSID "{35BC7016-AE11-429F-92C2-533B9E6D542C}"
#define IVX_TTSENGINE_CLSID_W L"{35BC7016-AE11-429F-92C2-533B9E6D542C}"
