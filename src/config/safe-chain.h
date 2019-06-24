
#ifndef SAFE_CHAIN_H
#define SAFE_CHAIN_H

////////////////////////////////////////////////////////////
//pre-defined safe-chain-name

//notice:
//  SCN__`xxx` must not be 0
//  or `#if SCN_CURRENT == SCN__main` is always executed
#define SCN__error      -1
#define SCN__main       1
#define SCN__dev        10
#define SCN__test       20

////////////////////////////////////////////////////////////
#define SAFE_CHAIN_NAME dev

#if !defined(SAFE_CHAIN_NAME)
#pragma message("use default safe-chain-name: main")
#define SAFE_CHAIN_NAME main
#endif

////////////////////////////////////////////////////////////
#define _SCN_CAT(x)  SCN__##x
#define SCN_CAT(x)   _SCN_CAT(x)
#define SCN_CURRENT SCN_CAT(SAFE_CHAIN_NAME)

////////////////////////////////////////////////////////////

#if SCN_CURRENT == SCN__main
#pragma message("SCN_CURRENT == SCN__main")
#elif SCN_CURRENT == SCN__dev
#pragma message("SCN_CURRENT == SCN__dev")
#elif SCN_CURRENT == SCN__test
#pragma message("SCN_CURRENT == SCN__test")
#else
#define _DISPLAY_SCN_CURRENT(x) #x
#define DISPLAY_SCN_CURRENT(x) _DISPLAY_SCN_CURRENT(x)
#pragma message("error: SCN_CURRENT == " DISPLAY_SCN_CURRENT(SCN_CURRENT))
#error unsupported <safe chain name>
#endif

#endif  //SAFE_CHAIN_H
