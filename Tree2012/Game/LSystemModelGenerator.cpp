#include "pch.h"
#include "LSystemModelGenerator.h"
#include <stack>

float DefaultLength(LSystemParams* params, float cmdParam)
{
    return params->_segmentLength;
}

LSystemParams::LSystemParams() :
    _numIterations(0), _angle(0), _constants(), _axiom(), _rules()
{
    SegmentLength = DefaultLength;
};


LSystemModelGenerator::LSystemModelGenerator(LSystemParams& params) : TreeModelGenerator(), _params(params)
{
}


enum EquationType
{
    ET_UNKNOWN,
    ET_ADD,
    ET_MULTIPLY
};

enum ParamType
{
    PT_UNKNOWN,
    PT_FLOAT,
    PT_EQUATION
};

struct Param
{
    char raw[64];
    ParamType paramType;
    float floatVal;
    char symbol;
    EquationType equationType;
};

struct Command
{
    char symbol;
    Param param;
};

bool GetCommand(string::const_iterator& cmdIt, string::const_iterator end, ParamType paramType, Command* result)
{
    ZeroMemory(result, sizeof(Command));

    // Skip white space
    while (cmdIt != end && *cmdIt == ' ')
    {
        cmdIt++;
    }

    if (cmdIt == end)
    {
        return false;
    }

    result->symbol = *cmdIt;

    ParamType outputParamType = PT_UNKNOWN;
    float param = 0.0f;
    int decimalPosition = 0;
    bool parsingFloat = false;
    auto paramIt = cmdIt;
    while (paramIt + 1 != end)
    {
        paramIt++;

        if (paramIt == cmdIt + 1)  // First position
        {
            if (*paramIt != '(') // No param found
            {
                return true;
            }
        }
        else
        {
            if (*paramIt == ')') // Param end
            {
                cmdIt = paramIt; // Move iterator to end of param
                result->param.paramType = outputParamType;
                return true;
            }

            char ch = *paramIt;
            if (paramType == PT_EQUATION)
            {
                if (ch >= 'a' && ch <= 'z')
                {
                    result->param.symbol = ch;
                    outputParamType = PT_EQUATION;
                }

                switch (ch)
                {
                case '+':
                    result->param.equationType = ET_ADD;
                    break;
                case '*':
                    result->param.equationType = ET_MULTIPLY;
                    break;
                }
            }

            if (paramType == PT_FLOAT || paramType == PT_EQUATION)
            {
                if (ch >= '0' && ch <= '9')
                {
                    byte digit = ch - '0';
                    if (decimalPosition)
                    {
                        result->param.floatVal += digit / (10.0f * decimalPosition);
                        decimalPosition++;
                    }
                    else
                    {
                        result->param.floatVal *= 10.0f;
                        result->param.floatVal += digit;
                    }

                    outputParamType = outputParamType == PT_UNKNOWN ? PT_FLOAT : outputParamType;

                }
                else if (ch == '.')
                {
                    decimalPosition = 1;
                }
            }
        } 
    }

    return true;
}

void replaceAll(string& inout, const string &search, const string &replace)
{
    // First tokenize all strings
    Command searchCmd = {};

    for (auto searchIt = search.begin(); searchIt != search.end(); searchIt++)
    {
        GetCommand(searchIt, search.end(), PT_EQUATION, &searchCmd);
    }

    std::vector<Command> replaceCmds;
    for (auto& replaceIt = replace.begin(); replaceIt != replace.end(); replaceIt++)
    {
        Command repCmd = {};
        if (GetCommand(replaceIt, replace.end(), PT_EQUATION, &repCmd))
        {
            replaceCmds.push_back(repCmd);
        }
    }

    size_t pos = 0;
    std::vector<Command> inputCmds;
    for (auto inoutIt = inout.begin(); inoutIt != inout.end(); inoutIt++)
    {
        Command inoutCmd = {};
        if (GetCommand(inoutIt, inout.end(), PT_EQUATION, &inoutCmd))
        {
            inputCmds.push_back(inoutCmd);
        }
    }

    // Second do search/replace
    std::vector<Command> outputCmds;
    for (auto& inputCmd : inputCmds)
    {
        if (inputCmd.symbol == searchCmd.symbol)
        {
            for(auto replaceCmd : replaceCmds)
            {
                if (replaceCmd.param.paramType == PT_EQUATION)
                {
                    assert(inputCmd.param.paramType == PT_FLOAT);
                    if (replaceCmd.param.equationType == ET_MULTIPLY)
                    {
                        replaceCmd.param.floatVal = inputCmd.param.floatVal * replaceCmd.param.floatVal;
                    }
                    else if (replaceCmd.param.equationType == ET_ADD)
                    {
                        replaceCmd.param.floatVal = inputCmd.param.floatVal + replaceCmd.param.floatVal;
                    }

                    replaceCmd.param.paramType = PT_FLOAT;
                }

                outputCmds.push_back(replaceCmd);
            }
        }
        else
        {
            outputCmds.push_back(inputCmd);
        }
    }

    std::stringstream ss;

    for(auto& outputCmd : outputCmds)
    {
        ss << outputCmd.symbol;
        if (outputCmd.param.paramType != PT_UNKNOWN)
        {
            assert(outputCmd.param.paramType == PT_FLOAT);
            ss << '(';
            ss << outputCmd.param.floatVal;
            ss << ')';
        }
    }

    inout = ss.str();
}

TreeModel* LSystemModelGenerator::Create()
{
    _model = new TreeModel();

    string axiom = _params._axiom;
    for (int i = 0; i < _params._numIterations; i++)
    {
        for (auto r = _params._rules.begin(); r != _params._rules.end(); r++)
        {
            replaceAll(axiom, (*r).input, (*r).output);
        }
    }

    OutputDebugStringA(axiom.c_str());
    OutputDebugStringA("\n");
    CreateSkeleton(axiom);

    return _model;
}

void LSystemModelGenerator::CreateSkeleton(string& axiom)
{
    // Constants
    XMFLOAT3 xAxis(1, 0, 0);
    XMVECTOR xVec = XMLoadFloat3(&xAxis);
    XMFLOAT3 yAxis(0, 1, 0);
    XMVECTOR yVec = XMLoadFloat3(&yAxis);
    XMFLOAT3 zAxis(0, 0, 1);
    XMVECTOR zVec = XMLoadFloat3(&zAxis);
    XMVECTOR zeroVec = XMVectorSet(0,0,0,0);


    BuildState initialState;
    initialState.pos = XMVectorSet(0, 0, 0, 1);
    XMVECTOR startDir = XMVectorSet(0.0f, 1.0f, 0.0f, 0);

    //initialState.dir = XMQuaternionRotationNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XM_PI);
    initialState.matDir = XMMatrixIdentity();
    //initialState.matDir = XMMatrixLookToLH(initialState.pos, startDir, -zVec);
    //initialState.matDir = XMMatrixRotationY(XM_PIDIV2);

    float xSign = 1.0f;
    float ySign = 1.0f;
    float zSign = 1.0f;

    //XMVECTOR xQuadPos = XMQuaternionRotationAxis(xVec, _params._angle);
    //XMVECTOR xQuadNeg = XMQuaternionRotationAxis(xVec, -_params._angle);
    //XMVECTOR xQuad180 = XMQuaternionRotationAxis(xVec, XM_PI);
    XMMATRIX rotateXPosMat = XMMatrixRotationNormal(xVec, _params._angle * xSign);
    XMMATRIX rotateXNegMat = XMMatrixRotationNormal(xVec, _params._angle * -xSign);

    //XMVECTOR yQuadPos = XMQuaternionRotationAxis(XMLoadFloat3(&yAxis), -_params._angle);
    //XMVECTOR yQuadNeg = XMQuaternionRotationAxis(XMLoadFloat3(&yAxis), _params._angle);
    //XMVECTOR yQuad180 = XMQuaternionRotationAxis(yVec, XM_PI);
    XMMATRIX rotateYPosMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), _params._angle * ySign);
    XMMATRIX rotateYNegMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), _params._angle * -ySign);

    //XMVECTOR zQuadPos = XMQuaternionRotationAxis(XMLoadFloat3(&zAxis), _params._angle);
    //XMVECTOR zQuadNeg = XMQuaternionRotationAxis(XMLoadFloat3(&zAxis), -_params._angle);
    XMMATRIX rotateZPosMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), _params._angle * zSign);
    XMMATRIX rotateZNegMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), _params._angle * -zSign);

    XMMATRIX rotate180Mat = XMMatrixRotationNormal(yVec, XM_PI);


    // Create trunk
    int id = _model->treeData.numBranches++;
    Branch* child = &_model->treeData.pBranches[id];

    _model->trunk = child;
    _model->trunk->id = id;
    _model->trunk->parent = -1;
    XMStoreFloat4(&_model->trunk->start, initialState.pos);

    //XMQuaternionToAxisAngle(&axis, &angle, initialState.dir);
    //XMStoreFloat4(&_model->trunk->end, initialState.pos + axis * _params.SegmentLength(&_params, 0.0f));

    XMStoreFloat4(&_model->trunk->end, initialState.pos);
    _model->trunk->thickness = _params.thickness;
    _model->trunk->depth = 0;
    initialState.branch = _model->trunk;

    BuildState currentState = initialState;
    ///currentState.pos = XMLoadFloat4(&_model->trunk->end);

    //    _params._angle *= 2.0f; // WHy?

    //XMFLOAT4 startPosNMinus1(1.0f, .0f, 0, 1.0f);
    //XMVECTOR prevPos = XMLoadFloat4(&startPosNMinus1);

    stack<BuildState> stateStack;
    int pos = 0;
    string done, unknown;
    for (auto cmdIt = axiom.begin(); cmdIt != axiom.end(); cmdIt++)
    {
        Command command = {};
        if (!GetCommand(cmdIt, axiom.end(), PT_FLOAT, &command))
        {
            assert(false);
        }

        switch (command.symbol)
        {
        case '&':
        case 'Y':
            //currentState.dir = XMQuaternionMultiply(yQuadPos, currentState.dir);
            currentState.matDir = XMMatrixMultiply(currentState.matDir, rotateYPosMat);
            break;
        case '^':
        case 'y':
            //currentState.dir = XMQuaternionMultiply(yQuadNeg, currentState.dir);
            currentState.matDir = XMMatrixMultiply(currentState.matDir, rotateYNegMat);
            break;
        case '<':
        case '\\':
        case 'X':
            //currentState.dir = XMQuaternionMultiply(xQuadPos, currentState.dir);
            currentState.matDir = XMMatrixMultiply(currentState.matDir, rotateXPosMat);
            break;
        case '>':
        case '/':
        case 'x':
            //currentState.dir = XMQuaternionMultiply(xQuadNeg, currentState.dir);
            currentState.matDir = XMMatrixMultiply(currentState.matDir, rotateXNegMat);
            break;
        case '+':
        case 'Z':
            //currentState.dir = XMQuaternionMultiply(zQuadPos, currentState.dir);
            currentState.matDir = XMMatrixMultiply(currentState.matDir, rotateZPosMat);
            break;
        case '-':
        case 'z':
            //currentState.dir = XMQuaternionMultiply(zQuadNeg, currentState.dir);
            currentState.matDir = XMMatrixMultiply(currentState.matDir, rotateZNegMat);
            break;
        case '|':
            //currentState.dir = XMQuaternionMultiply(yQuad180, currentState.dir);
            currentState.matDir = XMMatrixMultiply(currentState.matDir, rotate180Mat);
            break;
        case 'F':
        case 'L':
            {
                float cmdParam = command.param.paramType == PT_FLOAT ? command.param.floatVal : 0.0f;
                float len = _params.SegmentLength(&_params, cmdParam);

#if 0
                //XMVECTOR axis = XMVectorSet(0, 0, 0, 0);
                //float angle = 0;
                ////axis = XMVector3TransformNormal(yVec, XMMatrixRotationQuaternion(currentState.dir));
                //XMQuaternionToAxisAngle(&axis, &angle, currentState.dir);

                //XMFLOAT4 axisFloat4;
                //XMStoreFloat4(&axisFloat4, axis);
                //axisFloat4.w = 0;
                //axis = XMLoadFloat4(&axisFloat4);

                //XMVECTOR prevPos = currentState.pos;
                //currentState.pos = currentState.pos + axis * len * (command.symbol == 'L' ? 0.5f : 1.0f);
#else
                //XMVECTOR quat = XMQuaternionRotationMatrix(currentState.matDir);
                //XMVECTOR axis;  float angle;
                //XMQuaternionToAxisAngle(&axis, &angle, quat);
                //axis = XMVector3Normalize(axis);

                XMVECTOR axis = XMVector3Transform(yVec, currentState.matDir);

                XMVECTOR prevPos = currentState.pos;
                currentState.pos = currentState.pos + axis * len * (command .symbol == 'L' ? 0.5f : 1.0f);
#endif
                XMFLOAT4 tmpPrev, tmpNext;
                XMStoreFloat4(&tmpPrev, prevPos);
                XMStoreFloat4(&tmpNext, currentState.pos);

                currentState.branch = AddBranch(currentState.branch, tmpPrev, tmpNext, (command.symbol == 'L' ? Leaf : Stick), _params.thickness);
            }

            break;
        case '[':
            stateStack.push(currentState);
            break;
        case ']':
            currentState = stateStack.top();
            stateStack.pop();
            break;
        case 'C':
            // TODO color
            //cmdIt++;
            break;
        case ' ':
            break; // noop
        default:
            unknown.push_back(command.symbol);
            break;
        }

        pos++;
        done.push_back(command.symbol);
    }

    LOG(unknown.c_str());
}

