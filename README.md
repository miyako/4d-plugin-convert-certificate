# 4d-plugin-convert-certificate
Convert P12 (PFX, PKCS#12) format certificate to PEM

### Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
||<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|

### Version

<img width="32" height="32" src="https://user-images.githubusercontent.com/1725068/73986501-15964580-4981-11ea-9ac1-73c5cee50aae.png"> <img src="https://user-images.githubusercontent.com/1725068/73987971-db2ea780-4984-11ea-8ada-e25fb9c3cf4e.png" width="32" height="32" />

### Releases

[Releases](https://github.com/miyako/4d-plugin-convert-certificate/releases): `2.0.0` is the latest published build. New releases are tagged `vX.Y.Z`.

---

# Convert certificate

Converts a PKCS#12 file (`.p12`, `.pfx`) into a PEM certificate and a PEM private key, and creates an RSA key pair, a certificate signing request and a self-signed certificate. All the work is done by OpenSSL 3.5.9 (LTS), built from source and linked statically into the plugin, so nothing needs to be installed on the machine. (Up to `2.0.0` the plugin used OpenSSL 1.1.0g.) Every result is returned as PEM `Text` in an output parameter; neither command has a function result.

| Command | Returns | Purpose |
|---|---|---|
| [P12 TO PEM](#p12-to-pem) | certificate and private key (`Text`, PEM) | Extract the certificate and the private key from a PKCS#12 `BLOB`. |
| [MAKE CERTIFICATE](#make-certificate) | request, certificate, public key, private key (`Text`, PEM) | Generate a new RSA key pair, a signing request and a self-signed certificate. |

**Platforms:** macOS 11.5 or later, Windows 64-bit.

---

## Requirements & platform notes

- **Failure is silent.** Neither command raises a 4D error or sets `OK`. When something goes wrong (wrong password, invalid data, unknown field name, …) the affected output parameters are returned as empty strings. Test the outputs with `Length` before using them.
- **Private keys are returned unencrypted.** Both commands return the private key as a plain PEM block with no passphrase (`-----BEGIN RSA PRIVATE KEY-----`, PKCS#1, for an RSA key), even [P12 TO PEM](#p12-to-pem), where you pass a password. Treat the text like a password: don't log it, and protect any file you write it to.
- **Key types.** [MAKE CERTIFICATE](#make-certificate) always creates an RSA key. [P12 TO PEM](#p12-to-pem) exports any key type (RSA, EC, Ed25519, …); see its description for the PEM format of each (after `2.0.0`; up to `2.0.0` it returned an empty key for anything but RSA).
- **Legacy algorithms.** OpenSSL 3 moved old algorithms (MDC2, RC2, RC4, single DES, …) into its *legacy* provider. The plugin loads it at startup (built in, nothing to install), so the `MDC2` digest and PKCS#12 files encrypted with RC2 or RC4 (exported by older tools) still work.
- **All parameters are mandatory.** There are no optional forms; pass a variable for every output parameter.
- **Thread-safe.** Both commands are declared thread-safe in the plugin manifest and can be called from preemptive processes.
- **Apple silicon.** Builds after `2.0.0` are universal (`arm64` + `x86_64`). The published `2.0.0` build contains an Intel (`x86_64`) binary only, so on Apple silicon it needs 4D running under Rosetta.

---

## P12 TO PEM

### Syntax

```4d
P12 TO PEM (pfx ; password ; certificate ; privateKey)
```

| Parameter | Type | Description |
|---|---|---|
| `pfx` | BLOB | → Content of a PKCS#12 file (`.p12`, `.pfx`) |
| `password` | Text | → Password of the PKCS#12 file (may be empty) |
| `certificate` | Text | ← Certificate in PEM format |
| `privateKey` | Text | ← Private key in PEM format (unencrypted; PKCS#1 for an RSA key) |
| Result | — | None: the results are returned in `certificate` and `privateKey` |

### Description

`P12 TO PEM` decodes the PKCS#12 data in `pfx` with `password` (passed to OpenSSL as UTF-8) and returns:

- in `certificate`, the certificate that belongs to the private key, as a `-----BEGIN CERTIFICATE-----` block;
- in `privateKey`, the private key, **not encrypted**, in the traditional format of its type: `-----BEGIN RSA PRIVATE KEY-----` (PKCS#1) for RSA, `-----BEGIN EC PRIVATE KEY-----` for EC. Key types that have no such format (Ed25519, Ed448, …) are returned as `-----BEGIN PRIVATE KEY-----` (PKCS#8). The password is only used to open the PKCS#12 file; it does not protect the returned key.

Both current PKCS#12 files (AES-256 + PBKDF2, the default of OpenSSL 3) and legacy ones (3DES or RC2-40, as written by `openssl pkcs12 -legacy`, OpenSSL 1.x and many older tools) can be read.

Any additional CA certificates stored in the PKCS#12 file are ignored; only the main certificate is returned.

If the data can't be decoded or the password is wrong, both outputs are empty. If the file contains no certificate, `certificate` is empty; if it contains no key, `privateKey` is empty. (Up to `2.0.0`, those cases handed a missing object to OpenSSL instead of skipping it, a non-RSA key gave an empty `privateKey`, and every call leaked the decoded key and certificate; fixed after `2.0.0`.)

### Example

Illustrative example (not from the plugin's own test database): read a `.pfx` file and write the two PEM files into the database folder.

```4d
var $pfx : Blob
var $path; $cert; $key : Text

$path:=Select document(""; ".pfx;.p12"; "Select a PKCS#12 file"; 0)
If (OK=1)
	DOCUMENT TO BLOB(Document; $pfx)
	P12 TO PEM($pfx; "my password"; $cert; $key)
	If ((Length($cert)#0) & (Length($key)#0))
		TEXT TO DOCUMENT(Get 4D folder(Database folder)+"cert.pem"; $cert; "us-ascii"; Document with LF)
		TEXT TO DOCUMENT(Get 4D folder(Database folder)+"key.pem"; $key; "us-ascii"; Document with LF)
	Else
		ALERT("Wrong password, or not a PKCS#12 file with a certificate and a key.")
	End if
End if
```

The file dialog and document commands differ between 4D versions; check the Language Reference for your version if one of them doesn't match.

---

## MAKE CERTIFICATE

### Syntax

```4d
MAKE CERTIFICATE (hash ; length ; days ; keys ; values ; request ; certificate ; publicKey ; privateKey)
```

| Parameter | Type | Description |
|---|---|---|
| `hash` | Longint | → Digest used to sign the request and the certificate (see the constants below) |
| `length` | Longint | → RSA key size in bits, e.g. `2048` |
| `days` | Longint | → Validity of the certificate in days, starting now |
| `keys` | Text array | → Names of the subject fields (`C`, `ST`, `L`, `O`, `OU`, `CN`, `emailAddress`, …) |
| `values` | Text array | → Values of the subject fields, in the same order as `keys` |
| `request` | Text | ← Certificate signing request (PKCS#10) in PEM format |
| `certificate` | Text | ← Self-signed X.509 certificate in PEM format |
| `publicKey` | Text | ← RSA public key in PEM format (PKCS#1) |
| `privateKey` | Text | ← RSA private key in PEM format (PKCS#1, unencrypted) |
| Result | — | None: the results are returned in the four output parameters |

Constants for `hash` (theme *Hashing Algorithm*):

| Constant | Value |
|---|---|
| `MD5` | 0 |
| `SHA1` | 1 |
| `SHA224` | 2 |
| `SHA256` | 3 |
| `SHA384` | 4 |
| `SHA512` | 5 |
| `MDC2` | 6 |
| `RIPEMD160` | 7 |

### Description

`MAKE CERTIFICATE` works in two steps:

1. It generates a new RSA key pair of `length` bits (public exponent 65537) and builds a certificate signing request whose subject is made from `keys` and `values`, signed with the `hash` digest. The request, the public key and the private key are returned in `request`, `publicKey` and `privateKey`.
2. It reads the request back, checks its signature, and issues a self-signed certificate from it: same subject and issuer, version 3, serial number `1`, valid from now for `days` days, signed with the new private key and the same digest. It is returned in `certificate`.

Details worth knowing:

- **`hash`**: any value outside 0 to 7 falls back to SHA-256. Use `SHA256` or stronger; MD5 and SHA-1 signatures are rejected by current browsers and operating systems, and `MDC2` and `RIPEMD160` signatures are understood by very few programs.
- **`length`**: 2048 or more is recommended. Key generation time grows quickly with the size. Values above 16384 return empty outputs without generating anything (fixed after `2.0.0`; up to `2.0.0` the process could stay busy for a very long time). OpenSSL 3 rejects sizes below 512 bits, which also leaves every output empty.
- **`days`**: a value so large that the end date can't be represented returns an empty `certificate` (fixed after `2.0.0`; up to `2.0.0` the multiplication into seconds overflowed).
- **`keys` / `values`**: the two arrays **must** have the same size; otherwise the subject is left empty, silently, and the request and certificate are still produced. Pairs are added in array order, **including element 0**; a pair whose key or value is empty is skipped, so an unused element 0 is harmless. Field names can be OpenSSL short names (`CN`), long names (`commonName`) or dotted OIDs; a pair with an unknown name, or with a value the field doesn't allow (for example a `C` that isn't two letters), is skipped silently. Values are passed to OpenSSL as UTF-8, so accented and non-Latin text (`東京都`) is stored correctly, as a `UTF8String` (after `2.0.0`; up to `2.0.0` values were passed as ASCII and non-ASCII text was garbled). Plain ASCII values are encoded as before.
- **`certificate`** contains no X.509 extensions (no Subject Alternative Name, no basic constraints). Current browsers ignore the `CN` when they check the host name and require a Subject Alternative Name, so they reject this certificate for HTTPS even after you trust it. It is fine for testing 4D's own TLS setup and for clients that don't check the host name.
- **`publicKey`** is a `-----BEGIN RSA PUBLIC KEY-----` block (PKCS#1), not the more common `-----BEGIN PUBLIC KEY-----` (SubjectPublicKeyInfo) form. **`privateKey`** is a `-----BEGIN RSA PRIVATE KEY-----` block without encryption.
- **`request`** is a PKCS#10 version 1 request (version field `0`). Up to `2.0.0` the plugin wrote the invalid version value `1`, which OpenSSL 3 and some certificate authorities reject; fixed after `2.0.0`.
- If key generation or the request fails, all four outputs are empty. If only the certificate step fails, `certificate` is empty and the other three outputs are still returned.

### Example

From the plugin's original README: create a certificate and key for 4D's web server, then restart it.

```4d
ARRAY TEXT($k;7)
ARRAY TEXT($v;7)

$k{1}:="C"
$k{2}:="ST"
$k{3}:="L"
$k{4}:="O"
$k{5}:="O"
$k{6}:="CN"
$k{7}:="emailAddress"

$v{1}:="JP"
$v{2}:="Tokyo"
$v{3}:="Setagaya"
$v{4}:="Miyako"
$v{5}:="Me"
$v{6}:="127.0.0.1"
$v{7}:="keisuke.miyako@4d.com"

MAKE CERTIFICATE (SHA256;2048;365;$k;$v;$req;$cert;$pubkey;$privkey)

WEB STOP SERVER

TEXT TO DOCUMENT(Get 4D folder(Database folder)+"req.csr";$req;"us-ascii";Document with CRLF)
TEXT TO DOCUMENT(Get 4D folder(Database folder)+"cert.pem";$cert;"us-ascii";Document with CRLF)
TEXT TO DOCUMENT(Get 4D folder(Database folder)+"key.pem";$privkey;"us-ascii";Document with CRLF)

WEB START SERVER

OPEN URL("https://127.0.0.1")
```

The example writes `cert.pem` and `key.pem` into the database folder because that is where 4D's web server looks for them. Browsers still warn about a self-signed certificate (see the note on Subject Alternative Name above).

Illustrative variation: create only a signing request to send to a certificate authority, and check the result.

```4d
ARRAY TEXT($k;0)
ARRAY TEXT($v;0)
APPEND TO ARRAY($k;"CN")
APPEND TO ARRAY($v;"www.example.com")
APPEND TO ARRAY($k;"O")
APPEND TO ARRAY($v;"Example Ltd")

MAKE CERTIFICATE (SHA256;3072;1;$k;$v;$req;$cert;$pubkey;$privkey)

If (Length($req)=0)
	ALERT("Key or request generation failed.")
End if
```

---

## Error handling & troubleshooting

- **All outputs empty after [P12 TO PEM](#p12-to-pem)**: the password is wrong or the BLOB is not PKCS#12 data. The command can't tell you which.
- **Certificate returned but `privateKey` empty**: the PKCS#12 file holds no private key (with `2.0.0`, also: the key is not RSA).
- **All outputs empty after [MAKE CERTIFICATE](#make-certificate)**: key generation failed, most likely because `length` is too small or above 16384.
- **Request and certificate have an empty subject**: `keys` and `values` don't have the same number of elements, or every pair has an empty key or value.
- **A subject field is missing**: OpenSSL didn't recognise its name, or its key or value was empty. Use names such as `C`, `ST`, `L`, `O`, `OU`, `CN`, `emailAddress`.
- **Garbled accented characters in the subject**: you are using `2.0.0` or older, which passed values as ASCII. Later builds store them as UTF-8.
- **Browser rejects the self-signed certificate even after you trust it**: the certificate has no Subject Alternative Name extension. Use another tool if you need a browser-trusted development certificate.
- **The private key is readable by anyone who gets the text**: both commands return it unencrypted. Store it with restricted access.
- **Plugin not loaded on Apple silicon**: the `2.0.0` release is Intel-only; use a later release, or run 4D under Rosetta.

---

## Quick reference

```4d
// PKCS#12 -> PEM
DOCUMENT TO BLOB($path; $pfx)
P12 TO PEM($pfx; $password; $certPEM; $keyPEM)

// new key pair + CSR + self-signed certificate
ARRAY TEXT($k;0)
ARRAY TEXT($v;0)
APPEND TO ARRAY($k;"CN")
APPEND TO ARRAY($v;"127.0.0.1")
MAKE CERTIFICATE(SHA256; 2048; 365; $k; $v; $csr; $certPEM; $pubPEM; $keyPEM)
```
