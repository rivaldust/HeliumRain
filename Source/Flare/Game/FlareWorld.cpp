
#include "../Flare.h"

#include "FlareWorld.h"
#include "FlareGame.h"
#include "FlareSector.h"
#include "FlareTravel.h"
#include "FlareFleet.h"


/*----------------------------------------------------
    Constructor
----------------------------------------------------*/

UFlareWorld::UFlareWorld(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UFlareWorld::Load(const FFlareWorldSave& Data)
{
	FLOG("UFlareWorld::Load");
	Game = Cast<AFlareGame>(GetOuter());
    WorldData = Data;

	// Init planetarium
	Planetarium = NewObject<UFlareSimulatedPlanetarium>(this, UFlareSimulatedPlanetarium::StaticClass());
	Planetarium->Load();

    // Load all companies
    for (int32 i = 0; i < WorldData.CompanyData.Num(); i++)
    {
		LoadCompany(WorldData.CompanyData[i]);
    }

	// Load sectors
	for (int32 OrbitalBodyIndex = 0; OrbitalBodyIndex < Game->GetSectorCatalog()->OrbitalBodies.Num(); OrbitalBodyIndex++)
	{
		FFlareSectorCelestialBodyDescription* SectorCelestialBodyDescription = &Game->GetSectorCatalog()->OrbitalBodies[OrbitalBodyIndex];
		for (int32 OrbitIndex = 0; OrbitIndex < SectorCelestialBodyDescription->Orbits.Num(); OrbitIndex++)
		{
			FFlareSectorOrbitDescription* SectorOrbitDescription = &SectorCelestialBodyDescription->Orbits[OrbitIndex];
			for (int32 SectorIndex = 0; SectorIndex < SectorOrbitDescription->Sectors.Num(); SectorIndex++)
			{
				const FFlareSectorDescription* SectorDescription = &SectorOrbitDescription->Sectors[SectorIndex];

				// Find save if exist
				FFlareSectorSave* SectorSave = NULL;
				for (int32 i = 0; i < WorldData.SectorData.Num(); i++)
				{
					if (WorldData.SectorData[i].Identifier == SectorDescription->Identifier)
					{
						// Old save found
						SectorSave = &WorldData.SectorData[i];
						break;
					}
				}

				FFlareSectorSave NewSectorData;
				if (!SectorSave)
				{
					// No save, init new sector
					NewSectorData.GivenName = FText::FromString("");
					NewSectorData.Identifier = SectorDescription->Identifier;
					NewSectorData.LocalTime = 0;
					SectorSave = &NewSectorData;
				}

				FFlareSectorOrbitParameters OrbitParameters;
				OrbitParameters.CelestialBodyIdentifier = SectorCelestialBodyDescription->CelestialBodyIdentifier;
				OrbitParameters.Altitude = SectorOrbitDescription->Altitude;
				OrbitParameters.Phase = SectorDescription->Phase;

				LoadSector(SectorDescription, *SectorSave, OrbitParameters);
			}
		}
	}

	// Load all travels
	for (int32 i = 0; i < WorldData.TravelData.Num(); i++)
	{
		LoadTravel(WorldData.TravelData[i]);
	}

	// Companies post load
	for (int i = 0; i < Companies.Num(); i++)
	{
		Companies[i]->PostLoad();
	}
}


UFlareCompany* UFlareWorld::LoadCompany(const FFlareCompanySave& CompanyData)
{
    UFlareCompany* Company = NULL;

    // Create the new company
	Company = NewObject<UFlareCompany>(this, UFlareCompany::StaticClass(), CompanyData.Identifier);
    Company->Load(CompanyData);
    Companies.AddUnique(Company);

	FLOGV("UFlareWorld::LoadCompany : loaded '%s'", *Company->GetCompanyName().ToString());

    return Company;
}


UFlareSimulatedSector* UFlareWorld::LoadSector(const FFlareSectorDescription* Description, const FFlareSectorSave& SectorData, const FFlareSectorOrbitParameters& OrbitParameters)
{
	UFlareSimulatedSector* Sector = NULL;

	// Create the new sector
	Sector = NewObject<UFlareSimulatedSector>(this, UFlareSimulatedSector::StaticClass(), SectorData.Identifier);
	Sector->Load(Description, SectorData, OrbitParameters);
	Sectors.AddUnique(Sector);

	FLOGV("UFlareWorld::LoadSector : loaded '%s'", *Sector->GetSectorName().ToString());

	return Sector;
}


UFlareTravel* UFlareWorld::LoadTravel(const FFlareTravelSave& TravelData)
{
	UFlareTravel* Travel = NULL;

	// Create the new travel
	Travel = NewObject<UFlareTravel>(this, UFlareTravel::StaticClass());
	Travel->Load(TravelData);
	Travels.AddUnique(Travel);

	FLOGV("UFlareWorld::LoadTravel : loaded travel for fleet '%s'", *Travel->GetFleet()->GetFleetName());

	return Travel;
}


FFlareWorldSave* UFlareWorld::Save(UFlareSector* ActiveSector)
{
	WorldData.CompanyData.Empty();
	WorldData.SectorData.Empty();
	WorldData.TravelData.Empty();

	TArray<FFlareSpacecraftSave> SpacecraftData;
	if (ActiveSector)
	{
		ActiveSector->Save(SpacecraftData);

		// Reload  spacecrafts. Have to be done before companies save
		for (int i = 0 ; i < SpacecraftData.Num(); i++)
		{
			UFlareSimulatedSpacecraft* Spacecraft = FindSpacecraft(SpacecraftData[i].Immatriculation);
			Spacecraft->Load(SpacecraftData[i]);
		}
	}

	// Companies
	for (int i = 0; i < Companies.Num(); i++)
	{
		UFlareCompany* Company = Companies[i];

		FLOGV("UFlareWorld::Save : saving company ('%s')", *Company->GetName());
		FFlareCompanySave* TempData = Company->Save();
		WorldData.CompanyData.Add(*TempData);
	}

	// Sectors
	for (int i = 0; i < Sectors.Num(); i++)
	{
		UFlareSimulatedSector* Sector = Sectors[i];
		FLOGV("UFlareWorld::Save : saving sector ('%s')", *Sector->GetName());

		FFlareSectorSave* TempData;
		if (ActiveSector && Sector->GetIdentifier() == ActiveSector->GetIdentifier())
		{
			FLOG("  sector saved as active sector");
			TempData = ActiveSector->Save(SpacecraftData);
		}
		else
		{
			TempData = Sector->Save();
		}
		WorldData.SectorData.Add(*TempData);
	}

	// Travels
	for (int i = 0; i < Travels.Num(); i++)
	{
		UFlareTravel* Travel = Travels[i];

		FLOGV("UFlareWorld::Save : saving travel for ('%s')", *Travel->GetFleet()->GetFleetName());
		FFlareTravelSave* TempData = Travel->Save();
		WorldData.TravelData.Add(*TempData);
	}

	return &WorldData;
}


void UFlareWorld::Simulate(int64 Duration)
{
	WorldData.Time += Duration;

	for (int TravelIndex = 0; TravelIndex < Travels.Num(); TravelIndex++)
	{
		Travels[TravelIndex]->Simulate(Duration);
	}

	// Process events
}

void UFlareWorld::FastForward()
{
	TArray<FFlareWorldEvent> NextEvents = GenerateEvents();

	if(NextEvents.Num() == 0)
	{
		// Nothing will append in futur
		return;
	}

	FFlareWorldEvent& NextEvent = NextEvents[0];

	if(NextEvent.Time <= WorldData.Time)
	{
		FLOGV("Fast forward fail: next event is in the past. Current time is %ld but next event time %ld", WorldData.Time, NextEvent.Time);
		return;
	}

	int64 TimeJump = NextEvent.Time - WorldData.Time;
	Simulate(TimeJump);
}

void UFlareWorld::ForceTime(int64 Time)
{
	int64 TimeJump = Time - WorldData.Time;
	WorldData.Time = Time;
	if (TimeJump > 0)
	{
		Simulate(TimeJump);
	}


}

inline static bool EventTimeComparator (const FFlareWorldEvent& ip1, const FFlareWorldEvent& ip2)
 {
	 return (ip1.Time < ip2.Time);
 }

TArray<FFlareWorldEvent> UFlareWorld::GenerateEvents(UFlareCompany* PointOfView)
{
	TArray<FFlareWorldEvent> NextEvents;

	// TODO Implements PointOfView

	// Generate travel events
	for (int TravelIndex = 0; TravelIndex < Travels.Num(); TravelIndex++)
	{
		FFlareWorldEvent TravelEvent;

		TravelEvent.Time = WorldData.Time + Travels[TravelIndex]->GetRemainingTravelDuration();
		NextEvents.Add(TravelEvent);
	}

	NextEvents.Sort(&EventTimeComparator);
	return NextEvents;
}


UFlareTravel* UFlareWorld::StartTravel(UFlareFleet* TravelingFleet, UFlareSimulatedSector* DestinationSector)
{
	if (TravelingFleet->IsTraveling())
	{
		TravelingFleet->GetCurrentTravel()->ChangeDestination(DestinationSector);
		return TravelingFleet->GetCurrentTravel();
	}
	else
	{
		// Make the fleet exit the sector
		UFlareSimulatedSector* OriginSector = TravelingFleet->GetCurrentSector();
		OriginSector->RetireFleet(TravelingFleet);

		// Create the travel
		FFlareTravelSave TravelData;
		TravelData.FleetIdentifier = TravelingFleet->GetIdentifier();
		TravelData.OriginSectorIdentifier = OriginSector->GetIdentifier();
		TravelData.DestinationSectorIdentifier = DestinationSector->GetIdentifier();
		TravelData.DepartureTime = GetTime();
		return LoadTravel(TravelData);
	}
}

void UFlareWorld::DeleteTravel(UFlareTravel* Travel)
{
	FLOGV("UFlareWorld::DeleteTravel : remove travel for fleet '%s'", *Travel->GetFleet()->GetFleetName());

	Travels.Remove(Travel);
}
