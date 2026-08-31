## PCB specifications

- Base material: FR-4
- Layers: 2
- Thickness: 1.6 mm
- Outer copper weight: 1 oz

Keep the other fabrication options at their defaults.

## PCBA specifications

Select Economic PCBA and bottom-side assembly only. The top-side and omitted parts are installed by hand. Enable **Confirm Parts Placement** so component orientations can be checked before production.

## Gerber, BOM, CPL

See the `electrical/mfg-lite` directory

Sometimes, JLCPCB's parser misses the part number indicated and claims to not be able to find a part, in these cases, open the BOM file and find the part number, paste it into the search box manually.

The part numbers chosen within the BOM are selected to have the highest availability, but sometimes a part can still be out-of-stock. Handle this situation however you need to.

The rotations in the component placement file should be correct for the provided BOM, but you should be careful and double check against JLCPCB's placement preview.

If a part is dangerously low on stock at JLCPCB, they offer the option for you to pre-order the parts, which means "place a hold on these parts for me".

## Cost Estimate

A build batch of 5 units can cost $150 before taxes and before shipping

This is only the cost for JLCPCB, there are additional costs for other components you have to source yourself and solder manually.
