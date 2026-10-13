# Profiles, conversions, view transforms, proofing (spec §5.5; D-10). Stage 1: ICC profiles through
# Little CMS, the named pipeline and the document color policy.
archmage_add_module(color
    SOURCES
        src/ColorProfile.cpp
        src/ColorPipeline.cpp
    PUBLIC_LINKS
        archmage_base
    PRIVATE_LINKS
        LCMS2::LCMS2)
