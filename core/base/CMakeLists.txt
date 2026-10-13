# Vocabulary types every core module uses (ADR-0102): Expected and Error (ADR-0101), typed identifiers
# (D-16), geometry and checked arithmetic (D-17), secure random bytes and SHA-256 (D-19).
archmage_add_module(base
    SOURCES
        src/Error.cpp
        src/Id.cpp
        src/Random.cpp
        src/Sha256.cpp
    PRIVATE_LINKS
        OpenSSL::Crypto)
