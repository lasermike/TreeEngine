#include "pch.h"
#include "LSystemModelGenerator.h"
#include <stack>

double DefaultLength(LSystemParams* params, double cmdParam)
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
    ET_MULTIPLY,
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
    Param params[2];
    int numParams;
};

// Find the next command eg. F(x * 2, 1) in the string and return it as result
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
    int sign = 1;
    while (paramIt + 1 != end)
    {
        paramIt++;

        if (paramIt == cmdIt + 1 && *paramIt != '(')  // First position after symbol must be paren open or we are done parsing this command
        {
            return true;
        }

        char ch = *paramIt;

        if (ch == ')') // Param end
        {
            cmdIt = paramIt; // Move iterator to end of param
            result->params[result->numParams - 1].paramType = outputParamType;
            result->params[result->numParams - 1].doubleVal *= sign;
            return true;
        }
        else if (ch == '(')
        {
            result->numParams = 1; // First param
        }
        else if (ch == '?') // rand
        {
            result->params[result->numParams - 1].doubleVal = rand() / (double)RAND_MAX;
        }

        // Equation parsing
        if (paramType == PT_EQUATION)
        {
            assert(ch < 'A' || ch > 'Z'); // Nest expressions NYI
            bool isValid = (ch >= 'a' && ch <= 'z');
            if (isValid)
            {
                result->params[result->numParams - 1].symbol = ch;
                outputParamType = PT_EQUATION;
            }

            switch (ch)
            {
            case '+':
                result->params[result->numParams - 1].equationType = ET_ADD;
                break;
            case '*':
                result->params[result->numParams - 1].equationType = ET_MULTIPLY;
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
                    result->params[result->numParams - 1].doubleVal += digit / (pow(10.0, (double) decimalPosition) );
                    decimalPosition++;
                }
                else
                {
                    result->params[result->numParams - 1].doubleVal *= 10.0;
                    result->params[result->numParams - 1].doubleVal += digit;
                }

                outputParamType = outputParamType == PT_NONE ? PT_DOUBLE : outputParamType;
            }
            else if (ch == '.')
            {
                decimalPosition = 1;
            }
            else if (ch == '-')
            {
                sign = -sign;
            }
        }
    }

    return true;
}

void replaceAll(string& inout, const string &search, const string &replace)
{
    // Tokenize command to search for
    Command searchCmd = {};
    int i = 0;
    for (auto searchIt = search.begin(); searchIt != search.end(); searchIt++, i++)
    {
        GetCommand(searchIt, search.end(), PT_EQUATION, &searchCmd);
        assert(i == 0); // Should never search for more than one command at a time 
    }

    // Tokenize replace string into commands
    std::vector<Command> replaceCmds;
    for (auto& replaceIt = replace.begin(); replaceIt != replace.end(); replaceIt++)
    {
        Command repCmd = {};
        if (!GetCommand(replaceIt, replace.end(), PT_EQUATION, &repCmd))
        {
            break;
        }

        replaceCmds.push_back(repCmd);
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
                if (replaceCmd.params[0].paramType == PT_EQUATION)
                {
                    assert(inputCmd.params[0].paramType == PT_DOUBLE);
                    if (replaceCmd.params[0].equationType == ET_MULTIPLY)
                    {
                        replaceCmd.params[0].doubleVal = inputCmd.params[0].doubleVal * replaceCmd.params[0].doubleVal;
                    }
                    else if (replaceCmd.params[0].equationType == ET_ADD)
                    {
                        replaceCmd.params[0].doubleVal = inputCmd.params[0].doubleVal + replaceCmd.params[0].doubleVal;
                    }
                    else
                    {
                        replaceCmd.params[0].doubleVal = inputCmd.params[0].doubleVal;
                    }

                    replaceCmd.params[0].paramType = PT_DOUBLE;
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
        if (outputCmd.params[0].paramType != PT_NONE)
        {
            assert(outputCmd.params[0].paramType == PT_DOUBLE);
            ss << '(';
            ss << outputCmd.params[0].doubleVal;
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
    initialState.thickness = 1.0f;

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

        double magnitude = 1.0f;
        if (command.params[0].paramType == PT_DOUBLE)
        {
            magnitude = command.params[0].doubleVal;
        }

        XMMATRIX rotateMat;

        switch (command.symbol)
        {
        case '&':
        case 'Y':
            rotateMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), float(_params._angle * magnitude));
            currentState.matDir = XMMatrixMultiply(rotateMat, currentState.matDir);
            break;
        case '^':
        case 'y':
            rotateMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), float(-_params._angle * magnitude));
            currentState.matDir = XMMatrixMultiply(rotateMat, currentState.matDir);
            break;
        case '<':
        case '\\':
        case 'X':
            rotateMat = XMMatrixRotationNormal(XMLoadFloat3(&xAxis), float(_params._angle * magnitude));
            currentState.matDir = XMMatrixMultiply(rotateMat, currentState.matDir);
            break;
        case '>':
        case '/':
        case 'x':
            rotateMat = XMMatrixRotationNormal(XMLoadFloat3(&xAxis), float(-_params._angle * magnitude));
            currentState.matDir = XMMatrixMultiply(rotateMat, currentState.matDir);
            break;
        case '+':
        case 'Z':
            rotateMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), float(_params._angle * magnitude));
            currentState.matDir = XMMatrixMultiply(rotateMat, currentState.matDir);
            break;
        case '-':
        case 'z':
            rotateMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), float(-_params._angle * magnitude));
            currentState.matDir = XMMatrixMultiply(rotateMat, currentState.matDir);
            break;
        case '|':
            currentState.matDir = XMMatrixMultiply(rotate180Mat, currentState.matDir);
            break;
        case 'F':
        case 'L':
            {
                magnitude *= _params.SegmentLength(&_params, magnitude);

                XMVECTOR axis = XMVector3Transform(initialDirection, currentState.matDir);

                XMVECTOR prevPos = currentState.pos;
                currentState.pos = currentState.pos + axis * float(magnitude);
                //currentState.pos = currentState.pos + axis * float(magnitude) * (command .symbol == 'L' ? 0.5f : 1.0f);  // TODO: hack, get rid of this

                XMFLOAT4 tmpPrev, tmpNext;
                XMStoreFloat4(&tmpPrev, prevPos);
                XMStoreFloat4(&tmpNext, currentState.pos);

                GeometryType geomtryType = (currentState.modelId == 1 || command.symbol == 'L') ? Leaf : Stick;

                currentState.branch = AddBranch(currentState.branch, tmpPrev, tmpNext, geomtryType, 
                                                currentState.thickness * _params.thickness);
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
        case '!': // set line width
            currentState.thickness = (float) magnitude;
            break;
        case '$':
            currentState.modelId = (int) magnitude;
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

