# What each TPT element does to Buddy

Made by going through src/simulation/elements with the rules of his ground map (OverlayBuddy.cpp). Temperatures
are each element's default; anything 400 C+ sets him on fire, 60 C+ hurts, -50 C and colder slows and hurts.

| Element | File | Effect on him |
|---|---|---|
| ACEL | ACEL | solid |
| ACID | ACID | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire; eats him (hurts), used up on him |
| AMTR | AMTR | gas: drifts round him |
| ANAR | ANAR | solid powder: he stands on it, buried he digs out |
| ARAY | ARAY | solid |
| BALL | BALL | solid |
| TNT | BANG | solid |
| BASE | BASE | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| BCLN | BCLN | solid |
| BCOL | BCOL | solid powder: he stands on it, buried he digs out |
| BGLA | BGLA | solid powder: he stands on it, buried he digs out |
| VACU | BHOL | solid; swallows him (back at the top); hot: hurts |
| BIRD | BIRD | solid powder: he stands on it, buried he digs out |
| BIZR | BIZR | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| BIZG | BIZRG | gas: drifts round him; cold: slows and hurts |
| BIZS | BIZRS | solid; hot: hurts |
| BMTL | BMTL | solid |
| BOMB | BOMB | solid powder: he stands on it, buried he digs out; blows up on him |
| BOYL | BOYL | gas: drifts round him |
| BRAY | BRAY | solid |
| BRCK | BRCK | solid |
| BREL | BREC | solid powder: he stands on it, buried he digs out |
| BRMT | BRMT | solid powder: he stands on it, buried he digs out |
| BTRY | BTRY | solid |
| BVBR | BVBR | solid powder: he stands on it, buried he digs out |
| C-5 | C5 | solid |
| CAUS | CAUS | gas: drifts round him; hurts |
| BUBW | CBNW | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| CFLM | CFLM | gas: drifts round him; cold: slows and hurts |
| CLNE | CLNE | solid |
| CLST | CLST | solid powder: he stands on it, buried he digs out |
| CNCT | CNCT | solid powder: he stands on it, buried he digs out |
| CO2 | CO2 | gas: drifts round him |
| COAL | COAL | solid |
| CONV | CONV | solid |
| CRAY | CRAY | solid |
| CRMC | CRMC | solid |
| DCEL | DCEL | solid |
| DESL | DESL | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| DEST | DEST | solid powder: he stands on it, buried he digs out; blows up on him |
| DEUT | DEUT | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| DLAY | DLAY | solid |
| DMG | DMG | solid powder: he stands on it, buried he digs out |
| DMND | DMND | solid |
| DRAY | DRAY | solid |
| DRIC | DRIC | solid; cold: slows and hurts |
| DSTW | DSTW | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| DTEC | DTEC | solid |
| DUST | DUST | solid powder: he stands on it, buried he digs out |
| DYST | DYST | solid powder: he stands on it, buried he digs out |
| EQVE | E116 | solid powder: he stands on it, buried he digs out |
| ELEC | ELEC | shocks him; hot: hurts |
| EMBR | EMBR | solid powder: he stands on it, buried he digs out; sets him on fire |
| EMP | EMP | solid |
| ETRD | ETRD | solid |
| EXOT | EXOT | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| FIGH | FIGH | a stick figure: he is a wall to it (they bump, no fight) |
| FILT | FILT | solid |
| FIRE | FIRE | gas: drifts round him; sets him on fire |
| FIRW | FIRW | solid powder: he stands on it, buried he digs out |
| FISH | FISH | solid powder: he stands on it, buried he digs out |
| FOG | FOG | gas: drifts round him |
| FRAY | FRAY | solid |
| FRME | FRME | solid |
| FRZW | FRZW | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire; cold: slows and hurts |
| FRZZ | FRZZ | solid powder: he stands on it, buried he digs out |
| FSEP | FSEP | solid powder: he stands on it, buried he digs out |
| FUSE | FUSE | solid |
| FWRK | FWRK | solid powder: he stands on it, buried he digs out |
| GAS | GAS | gas: drifts round him |
| GBMB | GBMB | solid powder: he stands on it, buried he digs out |
| GEL | GEL | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| GLAS | GLAS | solid |
| GLOW | GLOW | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| GOLD | GOLD | solid |
| GOO | GOO | solid |
| GPMP | GPMP | solid |
| GRAV | GRAV | solid powder: he stands on it, buried he digs out |
| GRVT | GRVT | energy: passes him; with Newtonian gravity on, its pull moves him |
| GUN | GUNP | solid powder: he stands on it, buried he digs out |
| HYGN | H2 | gas: drifts round him |
| HEAC | HEAC | solid |
| HSWC | HSWC | solid |
| ICE | ICEI | solid; holds him (frozen in) |
| IGNC | IGNT | solid |
| INSL | INSL | solid |
| INST | INST | solid |
| INVS | INVIS | solid |
| INWR | INWR | solid |
| IRON | IRON | solid |
| ISOZ | ISOZ | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| ISZS | ISZS | solid; cold: slows and hurts |
| JOIN | JOIN | solid powder: he stands on it, buried he digs out |
| LAVA | LAVA | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire; sets him on fire |
| LCRY | LCRY | solid |
| LDTC | LDTC | solid |
| LIFE | LIFE | solid; sets him on fire |
| LIGH | LIGH | solid; shocks him |
| LITH | LITH | solid powder: he stands on it, buried he digs out |
| LN2 | LNTG | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire; cold: slows and hurts |
| LOXY | LO2 | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire; cold: slows and hurts |
| LOLZ | LOLZ | solid; hot: hurts |
| LOVE | LOVE | solid; hot: hurts |
| LRBD | LRBD | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire; hot: hurts |
| LSNS | LSNS | solid |
| MERC | MERC | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| METL | METL | solid |
| MORT | MORT | gas: drifts round him |
| MWAX | MWAX | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| BHOL | NBHL | solid |
| NBLE | NBLE | gas: drifts round him |
| NEUT | NEUT | hurts |
| NICE | NICE | solid; cold: slows and hurts |
| NITR | NITR | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| NSCN | NSCN | solid |
| NTCT | NTCT | solid |
| WHOL | NWHL | solid |
| OXYG | O2 | gas: drifts round him |
| OIL | OIL | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| PBCN | PBCN | solid |
| PCLN | PCLN | solid |
| PHOT | PHOT | warms him; sets him on fire |
| PIPE | PIPE | solid |
| C-4 | PLEX | solid |
| PLNT | PLNT | solid |
| PLSM | PLSM | gas: drifts round him; sets him on fire |
| PLUT | PLUT | solid powder: he stands on it, buried he digs out |
| POLO | POLO | solid powder: he stands on it, buried he digs out; hot: hurts |
| PPIP | PPIP | solid |
| PQRT | PQRT | solid powder: he stands on it, buried he digs out |
| PROT | PROT | hurts |
| PRTI | PRTI | solid; a portal: moves him to PRTO |
| PRTO | PRTO | solid |
| PSCN | PSCN | solid |
| PSNS | PSNS | solid |
| PSTE | PSTE | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| PSTN | PSTN | solid |
| PSTS | PSTS | solid |
| PTCT | PTCT | solid |
| PTNM | PTNM | solid |
| PUMP | PUMP | solid |
| PVOD | PVOD | solid |
| QRTZ | QRTZ | solid |
| RBDM | RBDM | solid |
| RFGL | RFGL | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| RFRG | RFRG | gas: drifts round him |
| RIME | RIME | solid |
| ROCK | ROCK | solid |
| RPEL | RPEL | solid |
| RSSS | RSSS | solid |
| RSST | RSST | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| SALT | SALT | solid powder: he stands on it, buried he digs out |
| SAND | SAND | solid powder: he stands on it, buried he digs out |
| SAWD | SAWD | solid powder: he stands on it, buried he digs out |
| SEED | SEED | solid powder: he stands on it, buried he digs out |
| SHLD | SHLD1 | solid |
| SHD2 | SHLD2 | solid |
| SHD3 | SHLD3 | solid |
| SHD4 | SHLD4 | solid |
| SING | SING | solid powder: he stands on it, buried he digs out; blows up on him |
| SLCN | SLCN | solid powder: he stands on it, buried he digs out |
| SLTW | SLTW | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| SMKE | SMKE | gas: drifts round him; hot: hurts |
| SNOW | SNOW | solid powder: he stands on it, buried he digs out |
| SOAP | SOAP | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| SPWN | SPAWN | solid |
| SPWN2 | SPAWN2 | solid |
| SPNG | SPNG | solid |
| SPRK | SPRK | solid; shocks him, runs into metal he touches |
| STKM | STKM | a stick figure: he is a wall to it (they bump, no fight) |
| STK2 | STKM2 | a stick figure: he is a wall to it (they bump, no fight) |
| STNE | STNE | solid powder: he stands on it, buried he digs out |
| STOR | STOR | solid |
| SWCH | SWCH | solid |
| TESC | TESC | solid |
| THDR | THDR | solid powder: he stands on it, buried he digs out; sets him on fire |
| THRM | THRM | solid powder: he stands on it, buried he digs out |
| TRON | TRON | solid; cold: slows and hurts |
| TSNS | TSNS | solid |
| TTAN | TTAN | solid |
| TUNG | TUNG | solid |
| URAN | URAN | solid powder: he stands on it, buried he digs out |
| VIBR | VIBR | solid |
| VINE | VINE | solid |
| VIRS | VIRS | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire; hurts; hot: hurts |
| VOID | VOID | solid; swallows him (back at the top) |
| VRSG | VRSG | gas: drifts round him; hurts; sets him on fire |
| VRSS | VRSS | solid; hurts |
| VSNS | VSNS | solid |
| WARP | WARP | gas: drifts round him |
| WATR | WATR | liquid: he floats or sinks by its weight; cool ones wet him, put out his fire |
| WAX | WAX | solid |
| VENT | WHOL | solid |
| WIFI | WIFI | solid |
| WWLD | WIRE | solid |
| WOOD | WOOD | solid |
| WTRV | WTRV | gas: drifts round him; hot: hurts |
| YEST | YEST | solid powder: he stands on it, buried he digs out |
