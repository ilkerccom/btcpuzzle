# Official Client for [btcpuzzle.info](https://btcpuzzle.info)

Created for btcpuzzle.info by [ilkerccom](https://github.com/ilkerccom), a bitcoin puzzle platform. Visit Btcpuzzle.info for a user guide. You can find all the necessary explanations on the website.

Guide => [How to use on btcpuzzle.info](https://btcpuzzle.info/how-to-join-pool)

API Documention => [btcpuzzle.info API Documentation](https://btcpuzzle.info/api-documentation)


# VanitySearch with Pool Mode
A version support custom range scanning and multi address scanning.

This is a modified version of VanitySearch by [JeanLucPons](https://github.com/JeanLucPons/VanitySearch/).

Performance optimization completed by [aaelick](https://github.com/aaelick). **Only one GPU per instance.**

Pool client mode created by [ilkerccom](https://github.com/ilkerccom) for btcpuzzle.info

# Build
## Windows

- Intall CUDA SDK and open VanitySearch.sln in Visual C++ 2017.
- You may need to reset your *Windows SDK version* in project properties.
- In Build->Configuration Manager, select the *Release* configuration.

- Note: The current relase has been compiled with CUDA SDK 10.0, if you have a different release of the CUDA SDK, you may need to update CUDA SDK paths in VanitySearch.vcxproj using a text editor. The current nvcc option are set up to architecture starting at 3.0 capability, for older hardware, add the desired compute capabilities to the list in GPUEngine.cu properties, CUDA C/C++, Device, Code Generation.

## Linux
- Edit the makefile and set up the appropriate CUDA SDK and compiler paths for nvcc.
    ```
    ccap=86
    
    ...
    
    CXX        = g++-9
    CUDA       = /usr/local/cuda-11.8
    CXXCUDA    = /usr/bin/g++-9
    ```

 - Build:
    ```
    $ make all
    ```
- **Attention!!! You need to use g++-9 or a lower version to compile, otherwise the program will not run properly.**


# Docker Build

- Build the image:
    ```
    $ docker build -t btcpuzzle .
    ```
- Run the container:
    ```
    $ docker run --gpus all -e USERTOKEN="Qq...RR" -e PUBKEY="-----BEGIN PUBLIC KEY-----|MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCwnK0l5Q1MSmx7kMjTywoQSQKc|NkYtdvUsb98zXgYacWz+hluStI21TtPzXcb9svDQR8a9h5jphVtG67h2m2txObET|2mtz8cIpk3+qkPThKRWVRlrR20o5OY2h3ovuzbUwJH2EjLc82yMj1dWfLVP47iXw|a2O/GKXLyRl37yUarwIDAQAB|-----END PUBLIC KEY-----" btcpuzzle
    ```

# Arguments

Example: ```./btcpuzzle -puzzle 71 ...```

```
-puzzle => Target puzzle number. Example: 71

-token => Your user token from btcpuzzle.info

-worker => Worker name (leave empty for auto-generated)

-apishare => Enable API share (Custom webhook) Example; http://example.com/webhook

-pubkey => Your RSA public key. Example: "-----BEGIN....". !Replace lines with | and wrap the key in double quotes.

-telegramtoken => Your Telegram bot token. Example: 123456789:ABCdefGhIJKlmNoPQRsTUVwXyZ

-telegramchatid => Your Telegram chat ID. Example: -123456789

```

# Default Settings

Update the ```pool.conf``` file.

```

# Client Pool Configuration
# https://btcpuzzle.info

# Required: Your user token from btcpuzzle.info
user_token=

# Worker name (leave empty for auto-generated)
worker_name=

# Target puzzle
target_puzzle=71

# GPU Settings
gpu_index=0

# Security
untrusted_computer=false
public_key=

# Telegram Notifications
telegram_share=false
telegram_token=
telegram_chat_id=

# API Share (Custom webhook)
api_share=false
api_share_url=

# Other Settings
custom_range=none

```

# Untrusted Computer and RSA Encryption

If you enter an RSA Public Key in the "pubkey" field, the "untrusted_computer" value will automatically be set to true. When a key is found, it will be encrypted using this public key and sent to you as a notification. Notifications can be delivered via the API or Telegram, depending on your configuration.

To decrypt the value, make sure you have the corresponding private key for the public key you provided. Only this private key can be used to decrypt the encrypted data.

You can generate these keys anywhere you prefer. Be sure to securely store both your public and private keys. The client will use the public key, and the private key must never be shared with anyone.

To ensure everything is working correctly, test these settings using puzzle 38 and verify that you can successfully decrypt the value.

# RSA Example Keys

RSA Generator and Decryption => https://emn178.github.io/online-tools/rsa/decrypt/

Example Private Key
```
-----BEGIN PRIVATE KEY-----
MIICdgIBADANBgkqhkiG9w0BAQEFAASCAmAwggJcAgEAAoGBAJmRjqctxB/Y5QYq
Tapk7fh1aGFyE8J7iG0O6ZQMujf5lx5k8GsXIoF4vuS6/62n6yHnHvwCEu4sZmMF
2hXRHOiymqsNdp23rarI0mlrv5qO0TQdEmZ6mKztAC6tfkdw/jOzOTOrZjS7bX3C
G3Cb9YN0Tenl+7C43DPxaRxCVez3AgMBAAECgYEAhEL01c945uTq+1Bb67FZs2+L
IsIZeprieOsrCTOc/rCcErVkyxb1xQS1hFH5+xpwTJa9/NXcb/0VgZt8pOWhOv1X
JKdQi960afjsOulHEw4fKktvbnTcJDpnKiSwG5vtKxH8f17hm3zgwLVZQggiFiGC
LQWiygl9s7wJh/sTQ0ECQQDvbff/ARMdrjuRor5qogzxi8fKqLRw1h3GTI6+0bAQ
huI7jMphcrHH5uD7Soyi7kVextH9CynIbZMuZ05CWCLpAkEApDJeado7hka6m22f
jMyDDONYpPNY8Sp9MoPIOeFUEf+7RlRc1AZLt1R6rbLeV6yF2u2VYUaFZVbt21kD
Objk3wJAQBN7Eii0d/YABSq7sQfrVN1mu6rIl4YF8+LbNOAjMVkXxH4aT1gFkg0M
2tOJrbT8pa+p1QGezf/dKscE36Z8uQJAAh34I6tBqziMPWbLcNhONENWKEJO+kUO
+jDCyyRBnj3K31xFGiK+pS18q3Kr9TtvOmRn0apEHAtj47khFoRwxwJAIpeEHGzS
dh1lMhUv4VPXlf2/TORZYpDpPEDJ6oN0O3d5jqsCBxBoXS/qkcGL+ExsBihdUoju
DGZ9adIJB8aINg==
-----END PRIVATE KEY-----

```

Example Public Key
```
-----BEGIN PUBLIC KEY-----
MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCZkY6nLcQf2OUGKk2qZO34dWhh
chPCe4htDumUDLo3+ZceZPBrFyKBeL7kuv+tp+sh5x78AhLuLGZjBdoV0Rzospqr
DXadt62qyNJpa7+ajtE0HRJmepis7QAurX5HcP4zszkzq2Y0u219whtwm/WDdE3p
5fuwuNwz8WkcQlXs9wIDAQAB
-----END PUBLIC KEY-----
```

Replace lines with ```|``` . Alternatively, you can also use the ```@``` symbol.

```
-pubkey "-----BEGIN PUBLIC KEY-----|MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCZkY6nLcQf2OUGKk2qZO34dWhh|chPCe4htDumUDLo3+ZceZPBrFyKBeL7kuv+tp+sh5x78AhLuLGZjBdoV0Rzospqr|DXadt62qyNJpa7+ajtE0HRJmepis7QAurX5HcP4zszkzq2Y0u219whtwm/WDdE3p|5fuwuNwz8WkcQlXs9wIDAQAB|-----END PUBLIC KEY-----"
```