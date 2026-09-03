// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file StreetMetrics.hpp
 * @brief Every real-world dimension the city is built from, in metres.
 *
 * This is the single most important file in the project. A city street reads as
 * fake long before anyone can say why, and the reason is almost always
 * proportion: a sidewalk you could park on, a car the size of a bus, a doorway
 * a giant would use. Putting every measurement in one place, with a note saying
 * where it comes from, is what keeps the whole scene in one scale.
 *
 * The district modelled here is a continental-European inner-city street of the
 * late 19th / early 20th century, with post-war and contemporary infill: 5–6
 * storey perimeter blocks, shops at street level, flats above. Dimensions follow
 * the conventions such a place is actually built to (German RASt 06 / EAE street
 * design practice, DIN 18065 for stairs and storey heights, StVO/VZKat sign
 * sizes, DIN 67523 for lighting geometry), rounded to the nearest sensible
 * building dimension. Nothing here reproduces a specific real street.
 *
 * Axes: +X east, +Y up, +Z north. The main street runs north–south along Z, the
 * side street east–west along X. Metres throughout; the renderer's world unit is
 * one metre and nothing scales it.
 */

namespace CnaStreet::Metrics {

// ---------------------------------------------------------------------------
// Carriageway
// ---------------------------------------------------------------------------
/// Main street travel lane. RASt 06 gives 3.25–3.50 m for an urban main road;
/// 3.30 m is the usual built width where buses run but trams do not.
inline constexpr float kMainLaneWidth = 3.30f;
/// Kerbside parking lane. 2.00 m is the minimum, 2.20 m the comfortable width.
inline constexpr float kParkingLaneWidth = 2.20f;
/// Left-turn pocket at the junction — the reason the street has more than two
/// lanes anywhere, and the reason the parking lane stops short of the corner.
inline constexpr float kTurnLaneWidth = 3.00f;
/// Kerb face to kerb face on the main street: parking + lane + lane + parking.
inline constexpr float kMainCarriagewayWidth =
    2.0f * kParkingLaneWidth + 2.0f * kMainLaneWidth;   // 11.00 m
/// Side street: one lane each way, no parking lane (bays are marked instead).
inline constexpr float kSideLaneWidth = 3.25f;
inline constexpr float kSideCarriagewayWidth = 2.0f * kSideLaneWidth;  // 6.50 m

// ---------------------------------------------------------------------------
// Footway
// ---------------------------------------------------------------------------
/// Main street footway. 2.50 m is the functional minimum; a shopping street
/// with street furniture, trees and a café strip is built at 3.5–4.0 m.
inline constexpr float kMainSidewalkWidth = 3.80f;
inline constexpr float kSideSidewalkWidth = 2.60f;
/// Kerb upstand. 12 cm minimum for drainage, 15 cm where buses pull in; 14 cm
/// is the common built value.
inline constexpr float kCurbHeight = 0.14f;
/// Dropped kerb at a crossing — not flush, so the tactile edge survives.
inline constexpr float kCurbDropHeight = 0.03f;
/// Width of the kerb stone itself, seen end-on from the carriageway.
inline constexpr float kCurbStoneWidth = 0.15f;
/// The gutter channel: two rows of setts laid flat against the kerb.
inline constexpr float kGutterWidth = 0.30f;
/// Building line to kerb line, i.e. the footway plus its kerb.
inline constexpr float kMainStreetHalfWidth =
    kMainCarriagewayWidth * 0.5f + kMainSidewalkWidth;   // 9.30 m
inline constexpr float kSideStreetHalfWidth =
    kSideCarriagewayWidth * 0.5f + kSideSidewalkWidth;   // 5.85 m

// ---------------------------------------------------------------------------
// Road markings
// ---------------------------------------------------------------------------
/// Standard longitudinal marking width.
inline constexpr float kLaneMarkWidth = 0.12f;
/// Broken centre line: 3 m painted, 6 m gap, inside a built-up area.
inline constexpr float kCentreLineMark = 3.00f;
inline constexpr float kCentreLineGap  = 6.00f;
/// Stop line. 0.50 m is the built width; it sits 1.0 m short of the crossing.
inline constexpr float kStopLineWidth = 0.50f;
inline constexpr float kStopLineSetback = 1.00f;
/// Zebra crossing: 0.50 m stripes with 0.50 m gaps, 4.0 m deep along the road.
inline constexpr float kZebraStripeWidth = 0.50f;
inline constexpr float kZebraStripeGap   = 0.50f;
inline constexpr float kZebraDepth       = 4.00f;
/// How far paint sits above the asphalt. Real thermoplastic is ~3 mm; this is a
/// depth-fighting margin, not a measurement, and is deliberately larger.
inline constexpr float kMarkingLift = 0.006f;

// ---------------------------------------------------------------------------
// Buildings
// ---------------------------------------------------------------------------
/// Commercial ground floor: high enough for a mezzanine and a shop sign.
inline constexpr float kGroundFloorHeight = 4.20f;
/// Residential upper storey, floor to floor. DIN 18065 practice.
inline constexpr float kUpperFloorHeight = 3.10f;
/// Contemporary office floor plate, slightly deeper than a flat's.
inline constexpr float kOfficeFloorHeight = 3.60f;
/// Parapet above the top slab on a flat-roofed building.
inline constexpr float kParapetHeight = 1.05f;
/// Wall thickness where a façade reveal is modelled.
inline constexpr float kWallThickness = 0.40f;
/// Window reveal depth: how far the glass sits behind the façade plane. This is
/// what stops a window reading as a sticker.
inline constexpr float kWindowReveal = 0.16f;
/// Typical double-casement window in a flat.
inline constexpr float kWindowWidth  = 1.15f;
inline constexpr float kWindowHeight = 1.65f;
/// Sill height above the floor.
inline constexpr float kWindowSill = 0.95f;
/// Frame member width.
inline constexpr float kWindowFrame = 0.06f;
/// A French window / balcony door.
inline constexpr float kBalconyDoorWidth  = 0.95f;
inline constexpr float kBalconyDoorHeight = 2.20f;
/// Residential entrance door.
inline constexpr float kEntranceDoorWidth  = 1.20f;
inline constexpr float kEntranceDoorHeight = 2.35f;
/// Shopfront glazing.
inline constexpr float kShopGlassHeight = 2.70f;
inline constexpr float kShopSillHeight  = 0.35f;
inline constexpr float kShopFasciaHeight = 0.65f;
/// Balcony.
inline constexpr float kBalconyDepth   = 1.25f;
inline constexpr float kBalconyWidth   = 2.70f;
inline constexpr float kBalconySlab    = 0.16f;
inline constexpr float kRailingHeight  = 1.05f;
/// Cornice projection and depth at the eaves.
inline constexpr float kCorniceProjection = 0.42f;
inline constexpr float kCorniceHeight     = 0.34f;
/// String course between storeys on an older façade.
inline constexpr float kStringCourseProjection = 0.09f;
inline constexpr float kStringCourseHeight     = 0.16f;
/// Plinth: the heavier base course at pavement level.
inline constexpr float kPlinthHeight     = 0.55f;
inline constexpr float kPlinthProjection = 0.06f;
/// Rainwater goods.
inline constexpr float kDownpipeRadius = 0.05f;
inline constexpr float kGutterRadius   = 0.06f;
/// Roof pitch for the older blocks, and the mansard break height.
inline constexpr float kRoofPitchDegrees = 42.0f;

// ---------------------------------------------------------------------------
// Street furniture
// ---------------------------------------------------------------------------
/// Street lighting column. DIN 67523: 8–10 m on a main road, 5–6 m on a
/// residential side street. Arm reaches over the kerb.
inline constexpr float kLampMainHeight = 8.00f;
inline constexpr float kLampSideHeight = 5.60f;
inline constexpr float kLampArmReach   = 1.55f;
inline constexpr float kLampColumnRadiusBase = 0.075f;
inline constexpr float kLampColumnRadiusTop  = 0.048f;
inline constexpr float kLuminaireLength = 0.66f;
inline constexpr float kLuminaireWidth  = 0.27f;
inline constexpr float kLuminaireHeight = 0.14f;

/// Vehicle signal head, three 200 mm aspects in one housing.
inline constexpr float kSignalLensRadius   = 0.10f;
inline constexpr float kSignalHousingWidth  = 0.30f;
inline constexpr float kSignalHousingHeight = 0.94f;
inline constexpr float kSignalHousingDepth  = 0.24f;
/// Height of the *bottom* of a vehicle head over the footway. StVO requires
/// 2.10 m minimum clearance; 2.25 m is normal on a kerbside post.
inline constexpr float kSignalMountHeight = 2.25f;
inline constexpr float kSignalPoleRadius  = 0.055f;
inline constexpr float kSignalPoleHeight  = 3.60f;
/// Pedestrian head: two aspects, mounted lower so it can be read from the kerb.
inline constexpr float kPedSignalHousingHeight = 0.64f;
inline constexpr float kPedSignalMountHeight   = 2.05f;
/// Mast arm over the carriageway on the primary approach.
inline constexpr float kSignalMastHeight = 6.00f;
inline constexpr float kSignalMastReach  = 5.20f;

/// Traffic sign faces. StVO VzKat "Größe 2" (the size used on urban roads).
inline constexpr float kSignRoundDiameter   = 0.60f;
inline constexpr float kSignTriangleSide    = 0.70f;
inline constexpr float kSignRectangleWidth  = 0.42f;
inline constexpr float kSignRectangleHeight = 0.63f;
inline constexpr float kSignPostRadius = 0.030f;
/// Lower edge of a sign over a footway: 2.20 m so nobody walks into it.
inline constexpr float kSignMountHeight = 2.20f;

/// Bench: 1.80 m long, seat 0.45 m, back 0.85 m.
inline constexpr float kBenchLength     = 1.80f;
inline constexpr float kBenchDepth      = 0.62f;
inline constexpr float kBenchSeatHeight = 0.45f;
inline constexpr float kBenchBackHeight = 0.86f;
/// Bollard.
inline constexpr float kBollardHeight = 0.90f;
inline constexpr float kBollardRadius = 0.055f;
inline constexpr float kBollardSpacing = 1.80f;
/// Litter bin on a post.
inline constexpr float kBinRadius = 0.19f;
inline constexpr float kBinHeight = 0.62f;
inline constexpr float kBinPostHeight = 1.05f;
/// Above-ground pillar hydrant.
inline constexpr float kHydrantHeight = 0.88f;
inline constexpr float kHydrantRadius = 0.10f;
/// Utility cabinet (Kabelverteiler) — the grey box every European street has.
inline constexpr float kCabinetWidth  = 0.86f;
inline constexpr float kCabinetDepth  = 0.36f;
inline constexpr float kCabinetHeight = 1.42f;
/// Manhole cover (DIN 4271 class D400) and gully grate.
inline constexpr float kManholeRadius = 0.31f;
inline constexpr float kGullyWidth  = 0.50f;
inline constexpr float kGullyDepth  = 0.30f;
/// Street name plate on a building corner.
inline constexpr float kStreetPlateWidth  = 0.68f;
inline constexpr float kStreetPlateHeight = 0.22f;
inline constexpr float kStreetPlateMount  = 2.85f;
/// Bicycle stand ("Anlehnbügel").
inline constexpr float kBikeRackWidth  = 0.75f;
inline constexpr float kBikeRackHeight = 0.80f;
inline constexpr float kBikeRackRadius = 0.024f;

// ---------------------------------------------------------------------------
// Vegetation
// ---------------------------------------------------------------------------
/// A mature street lime/plane: 9–13 m, clear stem to 2.6 m so a lorry passes.
inline constexpr float kTreeHeightMin   = 8.50f;
inline constexpr float kTreeHeightMax   = 12.50f;
inline constexpr float kTreeClearStem   = 2.60f;
inline constexpr float kTreeTrunkRadius = 0.15f;
inline constexpr float kTreeCrownRadius = 2.90f;
/// The open tree pit in the footway, with its cast-iron grating.
inline constexpr float kTreePitSize = 1.60f;

// ---------------------------------------------------------------------------
// Vehicles (exterior dimensions of the real classes they represent)
// ---------------------------------------------------------------------------
inline constexpr float kCarCityLength  = 3.60f, kCarCityWidth  = 1.68f, kCarCityHeight  = 1.52f;
inline constexpr float kCarHatchLength = 4.28f, kCarHatchWidth = 1.79f, kCarHatchHeight = 1.46f;
inline constexpr float kCarSedanLength = 4.71f, kCarSedanWidth = 1.83f, kCarSedanHeight = 1.44f;
inline constexpr float kCarEstateLength = 4.77f, kCarEstateWidth = 1.85f, kCarEstateHeight = 1.51f;
inline constexpr float kCarSuvLength   = 4.53f, kCarSuvWidth   = 1.87f, kCarSuvHeight   = 1.66f;
inline constexpr float kVanLength      = 5.34f, kVanWidth      = 2.00f, kVanHeight      = 2.36f;
/// Wheel geometry: a 205/55 R16 is 0.63 m across.
inline constexpr float kWheelRadius = 0.315f;
inline constexpr float kWheelWidth  = 0.215f;
/// Parking bay: 5.00 × 2.30 m for a parallel bay in a marked lane.
inline constexpr float kParkingBayLength = 5.40f;

// ---------------------------------------------------------------------------
// People
// ---------------------------------------------------------------------------
inline constexpr float kPersonHeightMin = 1.58f;
inline constexpr float kPersonHeightMax = 1.92f;
inline constexpr float kPersonShoulder  = 0.44f;
/// Eye height at the mean adult stature — the walking camera sits here.
inline constexpr float kEyeHeight = 1.66f;
/// Comfortable walking speed on a footway.
inline constexpr float kWalkSpeed = 1.35f;

// ---------------------------------------------------------------------------
// The block plan
// ---------------------------------------------------------------------------
/// How far the main street runs each way from the junction. 260 m of street in
/// front of the camera is enough to walk for two minutes without leaving it.
inline constexpr float kMainStreetHalfLength = 130.0f;
/// The side street is shorter and closed by a building at its far end.
inline constexpr float kSideStreetHalfLength = 62.0f;
/// Depth of a perimeter block from the street frontage to its back edge.
inline constexpr float kBlockDepth = 26.0f;
/// Width of one building plot along the frontage. Historic parcels vary; the
/// generator picks per building inside these bounds.
inline constexpr float kPlotWidthMin = 9.0f;
inline constexpr float kPlotWidthMax = 21.0f;

}  // namespace CnaStreet::Metrics
