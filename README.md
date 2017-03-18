# 4d-plugin-convert-certificate
Convert P12 (PFX, PKCS#12) format certificate to PEM

### Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|

### Version

<img src="https://cloud.githubusercontent.com/assets/1725068/18940649/21945000-8645-11e6-86ed-4a0f800e5a73.png" width="32" height="32" /> <img src="https://cloud.githubusercontent.com/assets/1725068/18940648/2192ddba-8645-11e6-864d-6d5692d55717.png" width="32" height="32" />

## Syntax

```
P12 TO PEM (pfx;password;certificate;privateKey)
```

Parameter|Type|Description
------------|------------|----
pfx|BLOB|P12 certificate (in)
password|TEXT|Password (in)
certificate|TEXT|Certificate (out)
privateKey|TEXT|RSA Private key (out)

```
MAKE CERTIFICATE (hash;length;days;keys;values;request;certificate;publicKey;privateKey)
```

Parameter|Type|Description
------------|------------|----
hash|INT32|Algorithm for keys (in)
length|INT32|Key length (in)
days|INT32|Certificate duration (in)
keys|ARRAY TEXT|Keys for certificate request (in)
values|ARRAY TEXT|Values certificate request (in)
request|TEXT|Certificate request in PEM format (out)
certificate|TEXT|Self-signed certificate in PEM format (out)
publicKey|TEXT|RSA public key in PEM format (out)
privateKey|TEXT|RSA private key in PEM format (out)

Constants for hash:

```c
MD5 0
SHA1 1
SHA224 2
SHA256 3
SHA384 4
SHA512 5
MDC2 6
RIPEMD160 7
```

### Example

```
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




