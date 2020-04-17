## Contents

```sh
axent_malta_bathtub
├── axent_malta_bathtub.c # axent_malta_bathtub source code
├── Config.in       # kconfig file
├── aos.mk          # aos build system file(for make)
└── k_app_config.h  # aos app config file
```

## Introduction

The **axent_malta_bathtub** ...

### Dependencies

### Supported Boards

- esp8266

### Build

```sh
# generate axent_malta_bathtub@esp8266 default config
aos make axent_malta_bathtub@esp8266 -c config

# build
aos make
```
