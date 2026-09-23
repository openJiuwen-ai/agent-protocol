from iiap import DecisionService


async def decide(model_adapter, packet):
    service = DecisionService(model_adapter)
    return await service.decide(packet)
