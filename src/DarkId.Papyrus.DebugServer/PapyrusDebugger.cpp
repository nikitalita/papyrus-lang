#include "PapyrusDebugger.h"

#include <functional>
#include <string>
#include <dap/protocol.h>
#include <dap/session.h>

#include "Utilities.h"
#include "GameInterfaces.h"
#include "StackStateNode.h"
#include "StackFrameStateNode.h"

#if SKYRIM
	#include <SKSE/Logger.h>
	#include <SKSE/API.h>
namespace XSE = SKSE;
#elif FALLOUT
	#include <F4SE/API.h>
	#include <F4SE/Logger.h>
namespace XSE = F4SE;
namespace RE{
	using BSSpinLockGuard = BSAutoLock<BSSpinLock, BSAutoLockDefaultPolicy>;
}
#endif

#include "StateNodeBase.h"


namespace DarkId::Papyrus::DebugServer
{
	PapyrusDebugger::PapyrusDebugger()
	{
		m_pexCache = std::make_shared<PexCache>();

		m_breakpointManager = std::make_shared<BreakpointManager>(m_pexCache.get());

		m_idProvider = std::make_shared<IdProvider>();
		m_runtimeState = std::make_shared<RuntimeState>(m_idProvider);

		m_executionManager = std::make_shared<DebugExecutionManager>(m_runtimeState.get(), m_breakpointManager.get());

	}

	void PapyrusDebugger::StartSession(std::shared_ptr<dap::Session> session) {
		m_closed = false;
		m_session = session;
		m_executionManager->Open(session);
		m_createStackEventHandle =
			RuntimeEvents::SubscribeToCreateStack(std::bind(&PapyrusDebugger::StackCreated, this, std::placeholders::_1));

		m_cleanupStackEventHandle =
			RuntimeEvents::SubscribeToCleanupStack(std::bind(&PapyrusDebugger::StackCleanedUp, this, std::placeholders::_1));

		m_instructionExecutionEventHandle =
			RuntimeEvents::SubscribeToInstructionExecution(
				std::bind(&PapyrusDebugger::InstructionExecution, this, std::placeholders::_1, std::placeholders::_2));

		// m_initScriptEventHandle = RuntimeEvents::SubscribeToInitScript(std::bind(&PapyrusDebugger::InitScriptEvent, this, std::placeholders::_1));
		m_logEventHandle =
			RuntimeEvents::SubscribeToLog(std::bind(&PapyrusDebugger::EventLogged, this, std::placeholders::_1));
		RegisterSessionHandlers();
	}
	void PapyrusDebugger::EndSession() {
		m_executionManager->Close();
		m_session = nullptr;
		m_closed = true;

		RuntimeEvents::UnsubscribeFromLog(m_logEventHandle);
		// RuntimeEvents::UnsubscribeFromInitScript(m_initScriptEventHandle);
		RuntimeEvents::UnsubscribeFromInstructionExecution(m_instructionExecutionEventHandle);
		RuntimeEvents::UnsubscribeFromCreateStack(m_createStackEventHandle);
		RuntimeEvents::UnsubscribeFromCleanupStack(m_cleanupStackEventHandle);

		m_executionManager->Close();
		// clear session data
		m_modDirectory = "";
		m_projectPath = "";
		m_projectSources.clear();
		m_breakpointManager->ClearBreakpoints();
		
	}

	void PapyrusDebugger::RegisterSessionHandlers(){
		// The Initialize request is the first message sent from the client and
		// the response reports debugger capabilities.
		// https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Initialize
		m_session->registerHandler([](const dap::InitializeRequest& request) {
			dap::InitializeResponse response;
			response.supportsConfigurationDoneRequest = true;
			response.supportsLoadedSourcesRequest = true;
			return response;
		});
		//sess->onError()
		m_session->registerSentHandler(
			[&](const dap::ResponseOrError<dap::InitializeResponse>&) {
				SendEvent(dap::InitializedEvent());
		});

		// Client is done configuring.
		m_session->registerHandler([&](const dap::ConfigurationDoneRequest&) {
			return dap::ConfigurationDoneResponse{};
		});

		// The Disconnect request is made by the client before it disconnects
		// from the server.
		// https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Disconnect
		m_session->registerHandler([&](const dap::DisconnectRequest&) {
			// Client wants to disconnect.
			return dap::DisconnectResponse{};
		});

		m_session->registerHandler([&](const dap::PDSAttachRequest& request) {
			return Attach(request);
		});
		m_session->registerHandler([&](const dap::PauseRequest& request) {
			return Pause(request);
		});
		m_session->registerHandler([&](const dap::ContinueRequest& request) {
			return Continue(request);
		});
		m_session->registerHandler([&](const dap::PauseRequest& request) {
			return Pause(request);
		});
		m_session->registerHandler([&](const dap::ThreadsRequest& request) {
			return GetThreads(request);
		});
		m_session->registerHandler([&](const dap::SetBreakpointsRequest& request) {
			return SetBreakpoints(request);
		});
		m_session->registerHandler([&](const dap::SetFunctionBreakpointsRequest& request) {
			return SetFunctionBreakpoints(request);
		});
		m_session->registerHandler([&](const dap::StackTraceRequest& request) {
			return GetStackTrace(request);
		});
		m_session->registerHandler([&](const dap::StepInRequest& request) {
			return StepIn(request);
		});
		m_session->registerHandler([&](const dap::StepOutRequest& request) {
			return StepOut(request);
		});
		m_session->registerHandler([&](const dap::NextRequest& request) {
			return Next(request);
		});
		m_session->registerHandler([&](const dap::ScopesRequest& request) {
			return GetScopes(request);
		});
		m_session->registerHandler([&](const dap::VariablesRequest& request) {
			return GetVariables(request);
		});
		m_session->registerHandler([&](const dap::SourceRequest& request) {
			return GetSource(request);
		});
		m_session->registerHandler([&](const dap::LoadedSourcesRequest& request) {
			return GetLoadedSources(request);
		});
	}

	template <typename T, typename>
	void PapyrusDebugger::SendEvent(const T& event) const{
		if (m_session)
			m_session->send(event);
	}

	std::string LogSeverityEnumStr(RE::BSScript::ErrorLogger::Severity severity) {
		if (severity == RE::BSScript::ErrorLogger::Severity::kInfo) {
			return std::string("INFO");
		} else if (severity == RE::BSScript::ErrorLogger::Severity::kWarning) {
			return std::string("WARNING");
		} else if (severity == RE::BSScript::ErrorLogger::Severity::kError) {
			return std::string("ERROR");
		} else if (severity == RE::BSScript::ErrorLogger::Severity::kFatal) {
			return std::string("FATAL");
		}
		return std::string("UNKNOWN_ENUM_LEVEL");
	}

	// EVENTS
	void PapyrusDebugger::LogGameOutput(RE::BSScript::ErrorLogger::Severity severity, const std::string &msg) const {
		constexpr const char* format = "GAME EVENT -- {}";
		if (severity == RE::BSScript::ErrorLogger::Severity::kInfo) {
			logger::info(format, msg);
		}
		else if (severity == RE::BSScript::ErrorLogger::Severity::kWarning) {
			logger::warn(format, msg);
		}
		else if (severity == RE::BSScript::ErrorLogger::Severity::kError) {
			logger::error(format, msg);
		}
		else if (severity == RE::BSScript::ErrorLogger::Severity::kFatal) {
			logger::critical(format, msg);
		}
	}

	void PapyrusDebugger::EventLogged(const RE::BSScript::LogEvent* logEvent) const
	{
		dap::OutputEvent output;
		output.category = "console";
#if SKYRIM
		output.output = std::string(logEvent->errorMsg) + "\r\n";
#elif FALLOUT
		RE::BSFixedString message;
		logEvent->errorMsg.GetErrorMsg(message);
		output.output = std::format("{} - {}\r\n", logEvent->ownerModule.c_str(), message.c_str());
#endif
		LogGameOutput(logEvent->severity, output.output);
		SendEvent(output);
	}

	void PapyrusDebugger::StackCreated(RE::BSTSmartPointer<RE::BSScript::Stack>& stack)
	{
		const auto stackId = stack->stackID;
		
		XSE::GetTaskInterface()->AddTask([this, stackId]()
		{
			if (m_closed)
			{
				return;
			}

			const auto stack = m_runtimeState->GetStack(stackId);
			if (!stack)
			{
				return;
			}

			SendEvent(dap::ThreadEvent{
				.reason = "started",
				.threadId = stackId
				});
			
			if (stack->top && stack->top->owningFunction)
			{
				// TODO: Not in use, just for debugging reference.
				auto srcFileName = stack->top->owningFunction->GetSourceFilename().c_str();
				auto scriptName = NormalizeScriptName(stack->top->owningObjectType->GetName());
				CheckSourceLoaded(scriptName.c_str());
			}
		});
	}
	
	void PapyrusDebugger::StackCleanedUp(uint32_t stackId)
	{
		XSE::GetTaskInterface()->AddTask([this, stackId]()
		{
			if (m_closed) return;

			SendEvent(dap::ThreadEvent{
				.reason = "exited",
				.threadId = stackId
			});
		});
	}

	void PapyrusDebugger::InstructionExecution(CodeTasklet* tasklet, CodeTasklet::OpCode opcode) const
	{
		m_executionManager->HandleInstruction(tasklet, opcode);
	}

	void PapyrusDebugger::CheckSourceLoaded(const char* scriptName) const
	{
		if (!m_pexCache->HasScript(scriptName))
		{
			dap::Source source;
			if (!m_pexCache->GetSourceData(scriptName, source))
			{
				return;
			}
			SendEvent(dap::LoadedSourceEvent{
				.reason = "new",
				.source = source
				});
		}
	}


	PapyrusDebugger::~PapyrusDebugger()
	{
		m_closed = true;

		RuntimeEvents::UnsubscribeFromLog(m_logEventHandle);
		// RuntimeEvents::UnsubscribeFromInitScript(m_initScriptEventHandle);
		RuntimeEvents::UnsubscribeFromInstructionExecution(m_instructionExecutionEventHandle);
		RuntimeEvents::UnsubscribeFromCreateStack(m_createStackEventHandle);
		RuntimeEvents::UnsubscribeFromCleanupStack(m_cleanupStackEventHandle);

		m_executionManager->Close();
	}
	
	dap::ResponseOrError<dap::InitializeResponse> PapyrusDebugger::Initialize(const dap::InitializeRequest& request)
	{
		return dap::ResponseOrError<dap::InitializeResponse>();
	}

	dap::ResponseOrError<dap::AttachResponse> PapyrusDebugger::Attach(const dap::PDSAttachRequest& request)
	{
		m_projectPath = request.projectPath.value("");
		m_modDirectory = request.modDirectory.value("");
		if (!request.restart.has_value())
		{
			m_pexCache->Clear();
		}
		for (auto src : request.projectSources.value(std::vector<dap::Source>())) {
			src.sourceReference = GetSourceReference(src);
			m_projectSources[src.sourceReference.value()] = src;
		}
		return dap::AttachResponse();
	}

	dap::ResponseOrError<dap::ContinueResponse> PapyrusDebugger::Continue(const dap::ContinueRequest& request)
	{
		if (m_executionManager->Continue())
			return dap::ContinueResponse();
		return dap::Error("Could not Continue");
	}

	dap::ResponseOrError<dap::PauseResponse> PapyrusDebugger::Pause(const dap::PauseRequest& request)
	{
		if (m_executionManager->Pause())
			return dap::PauseResponse();
		return dap::Error("Could not Pause");
	}

	dap::ResponseOrError<dap::ThreadsResponse> PapyrusDebugger::GetThreads(const dap::ThreadsRequest& request)
	{
		dap::ThreadsResponse response;
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSSpinLockGuard lock(vm->runningStacksLock);

		std::vector<std::string> stackIdPaths;

		for (auto& elem : vm->allRunningStacks)
		{
			const auto stack = elem.second.get();
			if (!stack || !stack->top)
			{
				continue;
			}

			stackIdPaths.push_back(std::to_string(stack->stackID));
		}

		for (auto& path : stackIdPaths)
		{
			std::shared_ptr<StateNodeBase> stateNode;
			if (!m_runtimeState->ResolveStateByPath(path, stateNode))
			{
				continue;
			}

			const auto node = dynamic_cast<StackStateNode*>(stateNode.get());

			dap::Thread thread;
			if (node->SerializeToProtocol(thread))
			{
				response.threads.push_back(thread);
			}
		}

		return response;
	}



	dap::ResponseOrError<dap::SetBreakpointsResponse> PapyrusDebugger::SetBreakpoints(const dap::SetBreakpointsRequest& request)
	{
		dap::Source source = request.source;
		auto ref = GetSourceReference(source);
		if (m_projectSources.find(ref) != m_projectSources.end()) {
			if (!CompareSourceModifiedTime(request.source, m_projectSources[ref])) {
				return dap::Error("Script has been modified after load");
			}
			source = m_projectSources[ref];
		}
		else {
			source.sourceReference = ref;
		}
		return m_breakpointManager->SetBreakpoints(source, request.breakpoints.value(std::vector<dap::SourceBreakpoint>()));
	}

	dap::ResponseOrError<dap::SetFunctionBreakpointsResponse> PapyrusDebugger::SetFunctionBreakpoints(const dap::SetFunctionBreakpointsRequest& request)
	{
		return dap::Error("unimplemented");
	}
	dap::ResponseOrError<dap::StackTraceResponse> PapyrusDebugger::GetStackTrace(const dap::StackTraceRequest& request)
	{
		dap::StackTraceResponse response;
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSSpinLockGuard lock(vm->runningStacksLock);

		if (request.threadId == -1)
		{
			response.totalFrames = 0;
			return dap::Error("No threadId specified");
		}

		std::vector<std::shared_ptr<StateNodeBase>> frameNodes;
		if (!m_runtimeState->ResolveChildrenByParentPath(std::to_string(request.threadId), frameNodes))
		{
			return dap::Error("Could not find ThreadId");
		}
		auto startFrame = request.startFrame.value(0);
		auto levels = request.levels.value(frameNodes.size());
		for (auto frameIndex = startFrame; frameIndex < frameNodes.size() && frameIndex < startFrame + levels; frameIndex++)
		{
			const auto node = dynamic_cast<StackFrameStateNode*>(frameNodes.at(frameIndex).get());

			dap::StackFrame frame;
			if (!node->SerializeToProtocol(frame, m_pexCache.get())) {
				return dap::Error("Serialization error");
			}

			response.stackFrames.push_back(frame);
		}
		return response;
	}
	dap::ResponseOrError<dap::StepInResponse> PapyrusDebugger::StepIn(const dap::StepInRequest& request)
	{
		// TODO: Support `granularity` and `target`
		if (m_executionManager->Step(request.threadId, STEP_IN)) {
			return dap::StepInResponse();
		}
		return dap::Error("Could not StepIn");
	}
	dap::ResponseOrError<dap::StepOutResponse> PapyrusDebugger::StepOut(const dap::StepOutRequest& request)
	{
		if (m_executionManager->Step(request.threadId, STEP_OUT)) {
			return dap::StepOutResponse();
		}
		return dap::Error("Could not StepOut");
	}
	dap::ResponseOrError<dap::NextResponse> PapyrusDebugger::Next(const dap::NextRequest& request)
	{
		if (m_executionManager->Step(request.threadId, STEP_OVER)) {
			return dap::NextResponse();
		}
		return dap::Error("Could not Next");
	}
	dap::ResponseOrError<dap::ScopesResponse> PapyrusDebugger::GetScopes(const dap::ScopesRequest& request)
	{
		dap::ScopesResponse response;
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSSpinLockGuard lock(vm->runningStacksLock);

		std::vector<std::shared_ptr<StateNodeBase>> frameScopes;
		if (!m_runtimeState->ResolveChildrenByParentId(request.frameId, frameScopes)) {
			return dap::Error("No scopes for frameId %d", request.frameId);
		}

		for (const auto& frameScope : frameScopes)
		{
			auto asScopeSerializable = dynamic_cast<IProtocolScopeSerializable*>(frameScope.get());
			if (!asScopeSerializable)
			{
				continue;
			}

			dap::Scope scope;
			if (!asScopeSerializable->SerializeToProtocol(scope))
			{
				continue;
			}
			
			response.scopes.push_back(scope);
		}

		return response;
	}

	dap::ResponseOrError<dap::VariablesResponse> PapyrusDebugger::GetVariables(const dap::VariablesRequest& request)
	{
		dap::VariablesResponse response;

		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSSpinLockGuard lock(vm->runningStacksLock);

		std::vector<std::shared_ptr<StateNodeBase>> variableNodes;
		if (!m_runtimeState->ResolveChildrenByParentId(request.variablesReference, variableNodes)) {
			return dap::Error("No such variable reference %d", request.variablesReference);
		}

		// TODO: support `start`, `filter`, parameter
		int count = 0;
		int maxCount = request.count.value(variableNodes.size());
		for (const auto& variableNode : variableNodes)
		{
			if (count > maxCount) {
				break;
			}
			auto asVariableSerializable = dynamic_cast<IProtocolVariableSerializable*>(variableNode.get());
			if (!asVariableSerializable)
			{
				continue;
			}

			dap::Variable variable;
			if (!asVariableSerializable->SerializeToProtocol(variable))
			{
				continue;
			}
			
			response.variables.push_back(variable);
			count++;
		}

		return response;
	}
	dap::ResponseOrError<dap::SourceResponse> PapyrusDebugger::GetSource(const dap::SourceRequest& request)
	{
		if (!request.source.has_value() || !request.source.value().name.has_value()) {
			if (!request.sourceReference) {
				return dap::Error("No source name or sourceReference");
			} else {
				// TODO: Support this?
				return dap::Error("No source name");
			}
		}
		auto ref = GetSourceReference(request.source.value());
		std::string name = request.source.value().name.value();
		dap::SourceResponse response;
		if (m_pexCache->GetDecompiledSource(name.c_str(), response.content)) {
			return response;
		}
		return dap::Error("Could not find source " + name);
	}

	dap::ResponseOrError<dap::LoadedSourcesResponse> PapyrusDebugger::GetLoadedSources(const dap::LoadedSourcesRequest& request)
	{
		dap::LoadedSourcesResponse response;
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSSpinLockGuard lock(vm->typeInfoLock);

		for (const auto& script : vm->objectTypeMap)
		{
			dap::Source source;
			std::string scriptName = script.first.c_str();
			if (m_pexCache->GetSourceData(scriptName.c_str(), source))
			{
				auto ref = GetSourceReference(source);
				// TODO: Get the modified times from the unlinked objects?
				if (m_projectSources.find(ref) != m_projectSources.end()) {
					response.sources.push_back(m_projectSources[ref]);
				}
				else {
					response.sources.push_back(source);
				}
			}
		}
		return response;
	}
}
