#pragma once

#include "CoreMinimal.h"

/**
 * Planet-local coordinates for the Next terrain renderer.
 *
 * Planet space is meters, double precision, origin at the moon center.
 * Render space is centimeters (1 uu = 1 cm) relative to the actor origin,
 * which Unreal's large-world camera subtracts in double before the GPU.
 * Absolute astronomical positions are never cast to float and then subtracted.
 *
 * Cube faces match DeepSpaceEngine TerrainTileGenerator.FacePointD /
 * PlanetTerrainRenderer.DirToFaceUV (commit 6c16ec7). Adapted algorithm, not GL.
 */
namespace PlanetTerrainNext
{
inline constexpr double CmPerMeter = 100.0;
inline constexpr double LunarRadiusMeters = 1737000.0;
inline constexpr double LunarRadiusUu = LunarRadiusMeters * CmPerMeter;
/** 1 AU in meters. Same sector size DeepSpaceEngine uses for UniversePosition. */
inline constexpr double AstronomicalUnitMeters = 1.495978707e11;

inline double MetersToUu(double Meters)
{
	return Meters * CmPerMeter;
}

/** Unit direction for cube face 0..5 and UV in [0,1]. */
inline FVector3d FacePoint(int32 Face, double U, double V)
{
	const double A = U * 2.0 - 1.0;
	const double B = V * 2.0 - 1.0;
	FVector3d Cube;
	switch (Face)
	{
	case 0: Cube = FVector3d(1.0, B, -A); break;
	case 1: Cube = FVector3d(-1.0, B, A); break;
	case 2: Cube = FVector3d(A, 1.0, -B); break;
	case 3: Cube = FVector3d(A, -1.0, B); break;
	case 4: Cube = FVector3d(A, B, 1.0); break;
	default: Cube = FVector3d(-A, B, -1.0); break;
	}
	return Cube.GetSafeNormal();
}

/**
 * Project a unit direction onto one face without winner-take-all.
 * Returns false when this face does not own the direction, including ties
 * only when the face's fixed axis is strictly smaller than another axis.
 * Edge and corner ties succeed for every face that contains the point.
 */
inline bool TryUnprojectFace(const FVector3d& Direction, int32 Face, double& OutU, double& OutV)
{
	const FVector3d D = Direction.GetSafeNormal();
	const double Ax = FMath::Abs(D.X);
	const double Ay = FMath::Abs(D.Y);
	const double Az = FMath::Abs(D.Z);
	const double Eps = 1.0e-9;
	double A = 0.0;
	double B = 0.0;
	switch (Face)
	{
	case 0:
		if (D.X <= 0.0 || Ax + Eps < Ay || Ax + Eps < Az) return false;
		A = -D.Z / D.X;
		B = D.Y / D.X;
		break;
	case 1:
		if (D.X >= 0.0 || Ax + Eps < Ay || Ax + Eps < Az) return false;
		A = -D.Z / D.X;
		B = -D.Y / D.X;
		break;
	case 2:
		if (D.Y <= 0.0 || Ay + Eps < Ax || Ay + Eps < Az) return false;
		A = D.X / D.Y;
		B = -D.Z / D.Y;
		break;
	case 3:
		if (D.Y >= 0.0 || Ay + Eps < Ax || Ay + Eps < Az) return false;
		A = -D.X / D.Y;
		B = -D.Z / D.Y;
		break;
	case 4:
		if (D.Z <= 0.0 || Az + Eps < Ax || Az + Eps < Ay) return false;
		A = D.X / D.Z;
		B = D.Y / D.Z;
		break;
	default:
		if (D.Z >= 0.0 || Az + Eps < Ax || Az + Eps < Ay) return false;
		A = D.X / D.Z;
		B = -D.Y / D.Z;
		break;
	}
	if (A < -1.0 - 1.0e-6 || A > 1.0 + 1.0e-6 || B < -1.0 - 1.0e-6 || B > 1.0 + 1.0e-6)
	{
		return false;
	}
	OutU = (A + 1.0) * 0.5;
	OutV = (B + 1.0) * 0.5;
	return true;
}

/** Unique face for an interior direction. Edge ties pick one face, matching DeepSpaceEngine. */
inline void DirectionToFaceUV(const FVector3d& Direction, int32& OutFace, double& OutU, double& OutV)
{
	const FVector3d D = Direction.GetSafeNormal();
	const double Ax = FMath::Abs(D.X);
	const double Ay = FMath::Abs(D.Y);
	const double Az = FMath::Abs(D.Z);
	double A = 0.0;
	double B = 0.0;
	if (Ax >= Ay && Ax >= Az)
	{
		if (D.X > 0.0)
		{
			OutFace = 0;
			A = -D.Z / D.X;
			B = D.Y / D.X;
		}
		else
		{
			OutFace = 1;
			A = -D.Z / D.X;
			B = -D.Y / D.X;
		}
	}
	else if (Ay >= Ax && Ay >= Az)
	{
		if (D.Y > 0.0)
		{
			OutFace = 2;
			A = D.X / D.Y;
			B = -D.Z / D.Y;
		}
		else
		{
			OutFace = 3;
			A = -D.X / D.Y;
			B = -D.Z / D.Y;
		}
	}
	else if (D.Z > 0.0)
	{
		OutFace = 4;
		A = D.X / D.Z;
		B = D.Y / D.Z;
	}
	else
	{
		OutFace = 5;
		A = D.X / D.Z;
		B = -D.Y / D.Z;
	}
	OutU = FMath::Clamp((A + 1.0) * 0.5, 0.0, 1.0);
	OutV = FMath::Clamp((B + 1.0) * 0.5, 0.0, 1.0);
}

/** Planet-local meters of a world point. Subtraction stays in double. */
inline FVector3d PlanetLocalMeters(const FVector& WorldPoint, const FVector& PlanetCenter)
{
	const FVector DeltaUu = WorldPoint - PlanetCenter;
	return FVector3d(DeltaUu.X, DeltaUu.Y, DeltaUu.Z) / CmPerMeter;
}

/** Camera-relative centimeters, still double. Subtract in meters before scaling. */
inline FVector CameraRelativeUu(const FVector3d& PointMeters, const FVector3d& CameraMeters)
{
	const FVector3d DeltaCm = (PointMeters - CameraMeters) * CmPerMeter;
	return FVector(DeltaCm.X, DeltaCm.Y, DeltaCm.Z);
}

/**
 * GPU presentation: double subtraction first, then a float. Near the camera the
 * delta is small, so the float has sub-centimeter precision even when both
 * absolute positions sit at astronomical coordinates.
 */
inline FVector3f CameraRelativeGpuUu(const FVector3d& PointMeters, const FVector3d& CameraMeters)
{
	const FVector3d DeltaCm = (PointMeters - CameraMeters) * CmPerMeter;
	return FVector3f(
		static_cast<float>(DeltaCm.X),
		static_cast<float>(DeltaCm.Y),
		static_cast<float>(DeltaCm.Z));
}

/** Incorrect path: cast absolute centimeters to float, then subtract. Used only by tests. */
inline FVector FloatFirstRelativeUu(const FVector3d& PointMeters, const FVector3d& CameraMeters)
{
	const FVector3d PointCm = PointMeters * CmPerMeter;
	const FVector3d CameraCm = CameraMeters * CmPerMeter;
	const FVector Point(static_cast<float>(PointCm.X), static_cast<float>(PointCm.Y), static_cast<float>(PointCm.Z));
	const FVector Camera(static_cast<float>(CameraCm.X), static_cast<float>(CameraCm.Y), static_cast<float>(CameraCm.Z));
	return Point - Camera;
}
}
