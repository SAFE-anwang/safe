
#ifndef SAFE_CHAIN_H
#define SAFE_CHAIN_H

////////////////////////////////////////////////////////////
//pre-defined safe-chain-name

#define SCN__error      -1
#define SCN__main       0
#define SCN__dev        10
#define SCN__test       20

////////////////////////////////////////////////////////////

#if !defined(SAFE_CHAIN_NAME)
#pragma message("use default safe-chain-name: main")
#define SAFE_CHAIN_NAME main
#endif

////////////////////////////////////////////////////////////

#define SCN_CURRENT (SCN__ ## SAFE_CHAIN_NAME)

////////////////////////////////////////////////////////////

#if SCN_CURRENT == SCN__main
#pragma message("SCN_CURRENT == SCN__main")
#elif SCN_CURRENT == SCN__dev
#pragma message("SCN_CURRENT == SCN__dev")
#elif SCN_CURRENT == SCN__test
#pragma message("SCN_CURRENT == SCN__test")
#else
#error "unsupported <safe chain name>"
#endif

#endif  //SAFE_CHAIN_H
