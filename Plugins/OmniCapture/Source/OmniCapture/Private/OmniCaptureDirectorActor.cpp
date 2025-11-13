#include "OmniCaptureDirectorActor.h"

#include "OmniCaptureSubsystem.h"

AOmniCaptureDirectorActor::AOmniCaptureDirectorActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void AOmniCaptureDirectorActor::Initialize(UOmniCaptureSubsystem* InSubsystem)
{
    Subsystem = InSubsystem;
}

void AOmniCaptureDirectorActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (Subsystem.IsValid())
    {
        Subsystem->TickCapture(DeltaSeconds);
    }
}

