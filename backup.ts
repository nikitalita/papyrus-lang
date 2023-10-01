

    







   






    makeStackFrameVarReqArgs(vpath: string[], frameId: number, emitErrors: boolean = true): SFDAP.VariablesArguments | undefined{
        if (frameId == -1) {
            const msg = "Cannot evaluate expression without a valid frameId!";
            this.logerror(msg);
            if (emitErrors) {
                this.emitOutputEvent("ERROR: " + msg, "console");
            }
            return undefined;
        }
        // get the stackframe
        let stackFrame = this._stackFrameMap.get(frameId);
        if (!stackFrame){ // this was probably in response to a REPL request; send an output event
            const msg = `Cannot find frame for frameId ${frameId}!`;
            this.logerror(msg);
            if (emitErrors){
                this.emitOutputEvent("ERROR: " + msg, "console");
            }
            return undefined;
        }
        let threadId = this.getThreadIdFromStackFrameId(frameId);
        if (!threadId) {
            const msg = `Cannot find threadId for frameId ${frameId}!`;
            this.logerror(msg);
            if (emitErrors) {
                this.emitOutputEvent("ERROR: " + msg, "console");
            }
            return undefined;
        }
        let realStackIndex = this.getRealStackIndex(stackFrame.id, threadId);
        let stackFrameRoot = {
            type: "stackFrame",
            threadId: threadId,
            stackFrameIndex: realStackIndex,
        } as SFDAP.Root;
        return {
            root: stackFrameRoot,
            path: vpath
        } as SFDAP.VariablesArguments;
    }

    createEvalValueRequest(expression: string | string[], frameId?: number, root?: SFDAP.Root, emitErrors: boolean = true): SFDAP.ValueRequest | undefined{
        if (!frameId && !root) return undefined;
        let vpath = StarfieldDebugAdapterProxy.getPathFromExpression(expression);
        if (root && root.type == "value"){
            return  <SFDAP.ValueRequest> new Request("value", {
                root: root,
                path: vpath
            });
        }
        if (frameId){
            if (root && root.type != "stackFrame") return undefined;
            const args = this.makeStackFrameVarReqArgs(vpath, frameId, emitErrors);
            return !args ? undefined : <SFDAP.ValueRequest> new Request("value", args);
        }
        return undefined;        
    }

    createEvalVariablesRequest(expression: string | string[], frameId?: number, root?: SFDAP.Root, emitErrors: boolean = true): SFDAP.VariablesRequest | undefined{
        if (!frameId && !root) return undefined;
        let vpath = StarfieldDebugAdapterProxy.getPathFromExpression(expression);
        if (root && root.type == "value"){
            return  <SFDAP.VariablesRequest> new Request("variables", {
                root: root,
                path: vpath
            });
        }
        if (frameId){
            if (root && root.type != "stackFrame") return undefined;
            const args = this.makeStackFrameVarReqArgs(vpath, frameId, emitErrors);
            return !args ? undefined : <SFDAP.VariablesRequest> new Request("variables", args);
        }
        return undefined;        
    }

        
    private handleREPLValueRequest(expr: string, evalRequest: DAP.EvaluateRequest) {
        // we need to check to see if we have the local scope for this frame
        let path = StarfieldDebugAdapterProxy.getPathFromExpression(expr);
        if (evalRequest.arguments.frameId){
            let frameId = evalRequest.arguments.frameId;
            if (!this._stackFrameMap.has(frameId)){
                this.sendErrorResponse(new Response(evalRequest), 1108, `Could not retrieve stackframe for ${frameId}`, null, ErrorDestination.User);
                return;
            }
            let localScope = this.findScopeForFrameAndPath(frameId, []);
            if (!localScope){
                
                let scopes = this.makeScopesforStackFrame(frameId)!;
                let localScope = scopes[0];
                let variablesRequest = this.createEvalVariablesRequest([], frameId);
                if (variablesRequest){
                    this.sendRequestToServerWithCB(variablesRequest, 10000, (r, req) => {
                        if (!r.success){
                            this.sendErrorResponse(new Response(evalRequest), 1108, `Could not retrieve local scope for ${frameId}`, null, ErrorDestination.User);
                            return;
                        }

                        let variables = this.addVariablesToState(r.body.variables, this._stackFrameMap.get(frameId)!, localScope);
                        let firstPart = path.length > 0 ? path[0] : "";
                        let found = variables.filter((v) => {
                            return v.name == firstPart;
                        });
                        if (!found){
                            // this is a global variable.
                            // TODO
                            this.sendErrorResponse(new Response(evalRequest), 1108, `Global var eval not supported yet`, null, ErrorDestination.User);
                            return;
                        }
                        // run this again
                        this.handleREPLValueRequest(expr, evalRequest);
                    });
                } else {
                    throw new Error("HOW?!?!?");
                }
                return;
            }
            let firstPart = path.length > 0 ? path[0] : "";
            let variables = this.getVariablesForScope(localScope);
            let found = variables.filter((v) => {
                return v.name == firstPart;
            });
            if (!found){
                // this is a global variable.
                // TODO
                this.sendErrorResponse(new Response(evalRequest), 1108, `Global var eval not supported yet`, null, ErrorDestination.User);
                return;
            }
        }
        let vreq = this.createEvalValueRequest(expr, evalRequest.arguments.frameId);
        if (vreq){
            this.sendRequestToServerWithCB(vreq, 10000, (r, req) => {
                this.handleEvalValueResponse(r as SFDAP.ValueResponse, req as SFDAP.ValueRequest, evalRequest);
            });
        } else {
            this.sendErrorResponse(new Response(evalRequest), 1108, "Invalid Evaluate request!!", null, ErrorDestination.User);
        }

    }

    private createEvalValueResponse(r: SFDAP.ValueResponse, req: SFDAP.ValueRequest, evalRequest: DAP.EvaluateRequest): DAP.EvaluateResponse{
        // TODO: Create variable node if it doesn't exist yet
        let evalResponseBody = {
            result: r.body.value,
            type: r.body.type,
            variablesReference: 0,
        };
        let evalResponse = <DAP.EvaluateResponse>new Response(evalRequest, r.message);
        evalResponse.success = r.success;
        evalResponse.body = evalResponseBody;
        return evalResponse;

    }
    private handleEvalValueResponse(r: SFDAP.ValueResponse, request: SFDAP.ValueRequest, evalRequest: DAP.EvaluateRequest) {
        let frameId = evalRequest.arguments.frameId;
        let evalType = request.arguments.root.type;
        this.emitOutputEvent(`Response to REPL value request (path: ${request.arguments.path.join(".")}):\n${colorize_message(r.body)}`, "console");
        let evalResponse = this.createEvalValueResponse(r, request, evalRequest);
        if (!r.success) {
            this.logerror(`Error getting value for variable request: ${r.message}`);
            this.sendMessageToClient(evalResponse);
            return;
        }
        let vresponse = r as SFDAP.ValueResponse;
        let thisScope = this.getOrCreateVariableScopesForPath(request.arguments.path, request.arguments.root, frameId);
        if (typeof thisScope == "string") {
            this.sendErrorResponse(new Response(evalRequest), 1106, thisScope, null, ErrorDestination.User);
            return;
        }
        let parentScope = this.getOrCreateVariableParentScopesForPath(request.arguments.path, request.arguments.root, frameId);
        if (typeof parentScope == "string") {
            this.sendErrorResponse(new Response(evalRequest), 1106, parentScope, null, ErrorDestination.User);
            return;
        }
        thisScope = thisScope as ScopeNode;
        if (vresponse.success == false) {
            this.logerror(`Error getting value for variable request: ${vresponse.message}`);
            // this doesn't stop the REPL from working, so don't send an error response
            let evalResponseBody = {
                result: "",
                variablesReference: thisScope.variablesReference,
            };
            evalResponse.body = evalResponseBody;
            this.sendMessageToClient(evalResponse);                
            return;
        }
        let thisVar = {
            name: request.arguments.path[request.arguments.path.length - 1],
            value: vresponse.body.value,
            type: vresponse.body.type,
            compound: true,
        } as SFDAP.Variable;
        let vvariable = new VariableNode(thisVar, thisScope.variablesReference, evalType != "stackFrame", parentScope as ScopeNode);
        this._variableMap.set(vvariable.variablesReference, vvariable);
        let evalResponseBody = {
            result: vvariable.value,
            type: vvariable.type,
            variablesReference: thisScope.variablesReference,
        };
        evalResponse.body = evalResponseBody;
        this.sendMessageToClient(evalResponse);
    }

    private handleREPLVariablesRequest(request: SFDAP.VariablesRequest, evalRequest: DAP.EvaluateRequest) {      
        this.sendRequestToServerWithCB(request, 10000, (r, req) => {
            this.handleEvalVariablesReqResponse(r as SFDAP.VariablesResponse, req as SFDAP.VariablesRequest, evalRequest);
        });
    }


    protected handleEvalVariablesReqResponse(response: SFDAP.VariablesResponse, request: SFDAP.VariablesRequest, evalRequest: DAP.EvaluateRequest, sendResponse?: boolean) {
        let evalType = request.arguments.root.type;
        let frameId = evalRequest.arguments.frameId;
        this.emitOutputEvent(`Response to REPL variables request (path: ${request.arguments.path.join(".")}):\n${colorize_message(response.body.variables)}`, "console");
        if (!sendResponse){
            return;
        }
        let evalResponse = <DAP.EvaluateResponse>new Response(evalRequest);
        if (response.success == false) {
            evalResponse.success = false;
            this.sendMessageToClient(evalResponse);
            return;
        }
        // // we know at this point it's a compound variable

        let variable: any = this.findVariableNodeForPath(request.arguments.path, request.arguments.root, frameId);
        if (!variable){
            // // we have to do a value request to find out what this is
            // let vreq = this.createEvalValueRequest(request.arguments.path, frameId, request.arguments.root, false);
            // if (!vreq) {
            //     this.logerror("Could not create value request for variable request!");
            //     // this doesn't stop the REPL from working, so don't send an error response
            // } else {
            //     this.sendRequestToServerWithCB(vreq, 10000, (r, req) => {
            //         this.handleEvalValueResponse(r as SFDAP.ValueResponse, req as SFDAP.ValueRequest, evalRequest);      
            //     });
            // }
            // return;
            variable = {
                value: "<unknown>",
                type: undefined
            } as any
        }
        let thisScope = this.getOrCreateVariableScopesForPath(request.arguments.path, request.arguments.root, frameId);
        if (typeof thisScope == "string") {
            this.logerror("Could not create scopes for variable request!");
            // this doesn't stop the REPL from working, so don't send an error response
        } 
        let evalResponseBody = {
            result: variable.value,
            type: variable.type,
            variablesReference: (thisScope as ScopeNode).variablesReference,
        };
        evalResponse.body = evalResponseBody;
        this.sendMessageToClient(evalResponse);

    }












    private findVariableNodeForPath(path: string[], root: SFDAP.Root, frameId?: number) {
        if (root.type == "value") {
            return undefined;
        }
        if (!frameId) {
            return undefined;
        }
        let scope = this.findScopeForFrameAndPath(frameId, path);
        if (!scope) {
            return undefined;
        }
        let variable = this._variableMap.get(scope.variablesReference);
        return variable;
    }

    static getPathFromExpression(expression: string | string[]): string[] {
        let vpath: string[];
        if (!Array.isArray(expression)){
            // split on SINGLE '.' to get the path
            vpath = expression.split(/\./);
            if (vpath.length == 1){
                vpath = vpath.filter((s)=>{return s.length > 0})
            }
        } else {
            vpath = expression;
        }
        return vpath;
    }
    private isVariableInScope(variable: VariableNode, scope: ScopeNode): boolean {
        return variable.parentScope == scope;
    }


    getVariablesForScope(localScope: ScopeNode) {
        let variables: VariableNode[] = [];
        this._variableMap.forEach((v) => {
            if (this.isVariableInScope(v, localScope)){
                variables.push(v);
            }
        });
        return variables;
    }
    private findScopeForFrameAndPath(frameId: number, path: string[]){
        return Array.from(this._scopeMap.values()).find((scope)=>{
            return scope.frameId == frameId && scope.path == path;
        });
    }
    private getOrCreateVariableParentScopesForPath( path: string[], root: SFDAP.Root, frameId?: number){
        if (root.type == "value") {
            // todo: handle this
            return "Can't do global variables yet!";
        }
        if (root.type != "stackFrame") {
            return "Invalid root type on REPL variables request!";
        }
        // this shouldn't happen; if this was a stackframe variable/value request, we should alreadyhave a stackframe
        if (!frameId) {
            return "No frameId on eval request!"
        }
        let parentScope = this.findScopeForFrameAndPath(frameId, path.slice(0, path.length - 1));
        if (parentScope) {
            return parentScope;
        }
        // lookup frame
        let stackFrame = this._stackFrameMap.get(frameId);
        // this shouldn't happen; if there's a frameId in the request, we should have a stackframe
        if (!stackFrame) {
            return `Could not find or create scopes for frameId ${frameId}!`;
        }
        
        let scopes = this.findScopesForFrame(frameId);
        if (!scopes || scopes.length == 0) {
            scopes = this.makeScopesforStackFrame(frameId)!;
        }
        if (!scopes) {
            return `Could not find or create scopes for frameId ${frameId}!`;
        }
        parentScope = scopes[0]; // start with local scope
        // for each path element, check if we have a scope for it
        // we already have the local scope, so we can skip that
        let lastParentRef = parentScope.variablesReference;
        for (let i = 0; i < path.length - 1; i++) {
            let curScope = scopes.find((scope) => {
                return scope.path == path.slice(0, i + 1);
            });
            // we have to make a new scope for each of the path elements that don't exist
            if (!curScope) {
                let pathElement = path[i];
                let iscope = {
                    name: pathElement,
                    path: path.slice(0, i + 1),
                    scopeType: pathElement == "self" ? "self" : "objectMember",
                    threadId: stackFrame.threadId,
                    frameId: frameId,
                    variablesReference: this.getVariableRefCount(),
                    parentVariableReference: lastParentRef,
                    expensive: false
                } as IScopeNode;
                curScope = new ScopeNode(iscope);
                this.addScopeToScopeMap(curScope);
                lastParentRef = curScope.variablesReference;
            }
            parentScope = curScope;
        }
        return parentScope;
    }


    private getOrCreateVariableScopesForPath( path: string[], root: SFDAP.Root, frameId?: number) {
        let evalType = root.type;
        if (root.type == "stackFrame" && !frameId) {
            return "No frameId on eval request!"
        }
        let scope = Array.from(this._scopeMap.values()).find((scope)=>{
            if (evalType == "value") {
                return scope.path == path && scope.scopeType == "reflectionItem";
            }
            return scope.path == path && (evalType == "stackFrame" ? scope.frameId == frameId : scope.scopeType == "reflectionItem");
        });

        if (!scope) {
            let parentScope = this.getOrCreateVariableParentScopesForPath(path, root, frameId)
            if (typeof parentScope == "string") {
                return parentScope;
            }
            let stackFrame = this._stackFrameMap.get(frameId!)!;
            let pathElement = path[path.length - 1];
            let iscope = {
                name: path[path.length - 1],
                path: path,
                scopeType: pathElement == "self" ? "self" : "objectMember",
                threadId: stackFrame.threadId,
                frameId: frameId,
                variablesReference: this.getVariableRefCount(),
                parentVariableReference: parentScope.variablesReference,
                expensive: false
            } as IScopeNode;
            scope = new ScopeNode(iscope);
        }
        return scope;
    }


