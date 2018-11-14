#include "pch.h"
#include "LSystemModelGenerator.h"
#include <stack>

float DefaultLength(LSystemParams* params, float cmdParam)
{
    return params->_segmentLength;
}

LSystemParams::LSystemParams() :
    _numIterations(0), _angle(0), _constants(), _axiom(), _rules(), _initialDirection(0.0, 1.0f, 0.0)
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
    PT_NONE,
    PT_DOUBLE,
    PT_EQUATION
};

struct Param
{
    char raw[64];
    ParamType paramType;
    double doubleVal;
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

    ParamType outputParamType = PT_NONE;
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
                assert(ch < 'A' || ch > 'Z');
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

            if (paramType == PT_DOUBLE || paramType == PT_EQUATION)
            {
                if (ch >= '0' && ch <= '9')
                {
                    int digit = ch - '0';
                    if (decimalPosition)
                    {
                        result->param.doubleVal += digit / (pow(10.0, (double) decimalPosition) );
                        decimalPosition++;
                    }
                    else
                    {
                        result->param.doubleVal *= 10.0;
                        result->param.doubleVal += digit;
                    }

                    outputParamType = outputParamType == PT_NONE ? PT_DOUBLE : outputParamType;

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
    // Tokenize search string
    Command searchCmd = {};

    for (auto searchIt = search.begin(); searchIt != search.end(); searchIt++)
    {
        GetCommand(searchIt, search.end(), PT_EQUATION, &searchCmd);
    }

    // Tokenize replace string
    std::vector<Command> replaceCmds;
    for (auto& replaceIt = replace.begin(); replaceIt != replace.end(); replaceIt++)
    {
        Command repCmd = {};
        if (GetCommand(replaceIt, replace.end(), PT_EQUATION, &repCmd))
        {
            replaceCmds.push_back(repCmd);
        }
    }

    // Tokenize input string
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

    // Do search/replace on input
    std::vector<Command> outputCmds;
    for (auto& inputCmd : inputCmds)
    {
        if (inputCmd.symbol == searchCmd.symbol)
        {
            for(auto replaceCmd : replaceCmds)
            {
                if (replaceCmd.param.paramType == PT_EQUATION)
                {
                    assert(inputCmd.param.paramType == PT_DOUBLE);
                    if (replaceCmd.param.equationType == ET_MULTIPLY)
                    {
                        replaceCmd.param.doubleVal = inputCmd.param.doubleVal * replaceCmd.param.doubleVal;
                    }
                    else if (replaceCmd.param.equationType == ET_ADD)
                    {
                        replaceCmd.param.doubleVal = inputCmd.param.doubleVal + replaceCmd.param.doubleVal;
                    }
                    else
                    {
                        replaceCmd.param.doubleVal = inputCmd.param.doubleVal;
                    }

                    replaceCmd.param.paramType = PT_DOUBLE;
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
        if (outputCmd.param.paramType != PT_NONE)
        {
            assert(outputCmd.param.paramType == PT_DOUBLE);
            ss << '(';
            ss << outputCmd.param.doubleVal;
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

    BuildState initialState;
    initialState.pos = XMVectorSet(0, 0, 0, 1);
    initialState.matDir = XMMatrixIdentity();

    XMVECTOR initialDirection = XMLoadFloat3(&_params._initialDirection);

    XMMATRIX rotateXPosMat = XMMatrixRotationNormal(xVec, _params._angle);
    XMMATRIX rotateXNegMat = XMMatrixRotationNormal(xVec, -_params._angle );
    XMMATRIX rotateYPosMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), _params._angle);
    XMMATRIX rotateYNegMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), -_params._angle);
    XMMATRIX rotateZPosMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), _params._angle);
    XMMATRIX rotateZNegMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), -_params._angle);
    XMMATRIX rotate180Mat = XMMatrixRotationNormal(yVec, XM_PI);

    // Create trunk
    int id = _model->treeData.numBranches++;
    Branch* child = &_model->treeData.pBranches[id];

    _model->trunk = child;
    _model->trunk->id = id;
    _model->trunk->parent = -1;
    XMStoreFloat4(&_model->trunk->start, initialState.pos);
    XMStoreFloat4(&_model->trunk->end, initialState.pos);
    _model->trunk->thickness = _params.thickness;
    _model->trunk->depth = 0;
    initialState.branch = _model->trunk;

    BuildState currentState = initialState;

    stack<BuildState> stateStack;
    int pos = 0;
    string done, unknown;
    for (auto cmdIt = axiom.begin(); cmdIt != axiom.end(); cmdIt++)
    {
        Command command = {};
        if (!GetCommand(cmdIt, axiom.end(), PT_DOUBLE, &command))
        {
            assert(false);
        }

        switch (command.symbol)
        {
        case '&':
        case 'Y':
            currentState.matDir = XMMatrixMultiply(rotateYPosMat, currentState.matDir);
            break;
        case '^':
        case 'y':
            currentState.matDir = XMMatrixMultiply(rotateYNegMat, currentState.matDir);
            break;
        case '<':
        case '\\':
        case 'X':
            currentState.matDir = XMMatrixMultiply(rotateXPosMat, currentState.matDir);
            break;
        case '>':
        case '/':
        case 'x':
            currentState.matDir = XMMatrixMultiply(rotateXNegMat, currentState.matDir);
            break;
        case '+':
        case 'Z':
            currentState.matDir = XMMatrixMultiply(rotateZPosMat, currentState.matDir);
            break;
        case '-':
        case 'z':
            currentState.matDir = XMMatrixMultiply(rotateZNegMat, currentState.matDir);
            break;
        case '|':
            currentState.matDir = XMMatrixMultiply(rotate180Mat, currentState.matDir);
            break;
        case 'F':
        case 'L':
            {
                double len = 0.0f;
                if (command.param.paramType == PT_DOUBLE)
                {
                    len = command.param.doubleVal;
                    len *= _params.SegmentLength(&_params, len);
                }
                else
                {
                    len = _params.SegmentLength(&_params, len);
                }

                XMVECTOR axis = XMVector3Transform(initialDirection, currentState.matDir);

                XMVECTOR prevPos = currentState.pos;
                currentState.pos = currentState.pos + axis * len * (command .symbol == 'L' ? 0.5f : 1.0f);  // TODO: hack, get rid of this

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

