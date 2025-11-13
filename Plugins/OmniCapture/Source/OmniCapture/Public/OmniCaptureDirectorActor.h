#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OmniCaptureDirectorActor.generated.h"

class UOmniCaptureSubsystem;

UCLASS(NotBlueprintable)
class OMNICAPTURE_API AOmniCaptureDirectorActor final : public AActor
{
    GENERATED_BODY()

public:
    AOmniCaptureDirectorActor();

    void Initialize(UOmniCaptureSubsystem* InSubsystem);

    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
    TWeakObjectPtr<UOmniCaptureSubsystem> Subsystem;
};

