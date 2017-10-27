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
    PT_SYMBOL,
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

bool GetCommand(string::const_iterator cmdIt, string::const_iterator end, ParamType paramType, Command* result)
{
    ZeroMemory(result, sizeof(Command));

    if (cmdIt == end || cmdIt + 1 == end)
    {
        return false;
    }

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
                return false;
            }
        }
        else
        {
            if (*paramIt == ')') // Param end
            {
                cmdIt = paramIt; // Move iterator to end of param
                result->param.paramType = paramType;
                return true;
            }

            char ch = *paramIt;
            if (paramType == PT_SYMBOL || paramType == PT_EQUATION)
            {
                if (ch >= 'a' && ch <= 'z')
                {
                    result->symbol = ch;
                }
            }

            if (paramType == PT_EQUATION)
            {
                switch (ch)
                {
                case '+':
                    result->param.equationType = ET_MULTIPLY;
                    break;
                case '*':
                    result->param.equationType = ET_MULTIPLY;
                    break;
                }
            }

            if (result->param.paramType == PT_FLOAT || result->param.paramType == PT_EQUATION)
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
                }
                else if (ch == '.')
                {
                    decimalPosition = 1;
                }
            }
        } 
    }

    return false;
}

void replaceAll(string& inout, const string &search, const string &replace)
{
    // First tokenize all strings
    Command searchCmd = {};

    for (auto searchIt = search.begin(); searchIt != search.end(); searchIt++)
    {
        GetCommand(searchIt, search.end(), PT_SYMBOL, &searchCmd);
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
            for(auto& replaceCmd : replaceCmds)
            {
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
    }

    inout = ss.str();

    //for (size_t pos = 0; ; pos += replace.length())
    //{
    //    // Locate the substring to replace
    //    pos = input.find(command.symbol, pos);
    //    if (pos == string::npos) break;
    //    // Replace by erasing and inserting
    //    ParamData inputParam = {};
    //    if (!GetParam(input.begin() + 1, input.end(), PT_EQUATION, &inputParam)
    //    {
    //        assert(false); // Maybe this should be allowed
    //    }

    //    ParamData replaceParam = {};
    //    if (!GetParam(replace.begin() + 1, replace.end(), PT_EQUATION, &replaceParam))
    //    {
    //        assert(false); // Maybe this should be allowed
    //    }

    //    int cmdLen = paramFound ? /*cmd(raw) */ : search.length();
    //    input.erase(pos, cmdLen);
    //    input.insert(pos, replace);
    //}
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
    BuildState initialState;
    initialState.pos = XMVectorSet(0, 0, 0, 1);
    initialState.dir = XMQuaternionRotationAxis(XMVectorSet(0, 1.0f, 0, 0), XM_PI);

    // Create trunk
    int id = _model->treeData.numBranches++;
    Branch* child = &_model->treeData.pBranches[id];

    _model->trunk = child;
    _model->trunk->id = id;
    _model->trunk->parent = -1;
    XMStoreFloat4(&_model->trunk->start, initialState.pos);


    XMVECTOR axis;  float angle;
    XMQuaternionToAxisAngle(&axis, &angle, initialState.dir);
    XMStoreFloat4(&_model->trunk->end, initialState.pos + axis * _params.SegmentLength(&_params, 0.0f));
    _model->trunk->thickness = _params.thickness;
    _model->trunk->depth = 0;
    initialState.branch = _model->trunk;

    BuildState currentState = initialState;
    currentState.pos = XMLoadFloat4(&_model->trunk->end);

    XMFLOAT3 zAxis(0, 0, 1);
    XMVECTOR zQuadPos = XMQuaternionRotationAxis(XMLoadFloat3(&zAxis), _params._angle);
    XMVECTOR zQuadNeg = XMQuaternionRotationAxis(XMLoadFloat3(&zAxis), -_params._angle);
    XMMATRIX rotateZPosMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), _params._angle);
    XMMATRIX rotateZNegMat = XMMatrixRotationNormal(XMLoadFloat3(&zAxis), -_params._angle);

    XMFLOAT3 xAxis(1, 0, 0);
    XMVECTOR xVec = XMLoadFloat3(&xAxis);
    XMVECTOR xQuadPos = XMQuaternionRotationAxis(xVec, _params._angle);
    XMVECTOR xQuadNeg = XMQuaternionRotationAxis(xVec, -_params._angle);
    XMVECTOR xQuad180 = XMQuaternionRotationAxis(xVec, XM_PI);
    XMMATRIX rotateXPosMat = XMMatrixRotationNormal(xVec, _params._angle);
    XMMATRIX rotateXNegMat = XMMatrixRotationNormal(xVec, -_params._angle);
    XMMATRIX rotate180Mat = XMMatrixRotationNormal(xVec, XM_PI);

    XMFLOAT3 yAxis(0, 1, 0);
    XMVECTOR yVec = XMLoadFloat3(&yAxis);
    XMVECTOR yQuadPos = XMQuaternionRotationAxis(XMLoadFloat3(&yAxis), _params._angle);
    XMVECTOR yQuadNeg = XMQuaternionRotationAxis(XMLoadFloat3(&yAxis), -_params._angle);
    XMMATRIX rotateYPosMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), _params._angle);
    XMMATRIX rotateYNegMat = XMMatrixRotationNormal(XMLoadFloat3(&yAxis), -_params._angle);

    stack<BuildState> stateStack;

    XMFLOAT4 tmpPrev, tmpNext;
    XMVECTOR prevPos;
    int pos = 0;
    string done, unknown;
    for (auto cmdIt = axiom.begin(); cmdIt != axiom.end(); cmdIt++)
    {
        char cmd = *cmdIt;
        switch (cmd)
        {
        case '&':
        case 'Y':
            currentState.dir = XMQuaternionMultiply(currentState.dir, yQuadPos);
            break;
        case '^':
        case 'y':
            currentState.dir = XMQuaternionMultiply(currentState.dir, yQuadNeg);
            break;
        case '<':
        case '\\':
        case 'X':
            currentState.dir = XMQuaternionMultiply(currentState.dir, xQuadPos);
            break;
        case '>':
        case '/':
        case 'x':
            currentState.dir = XMQuaternionMultiply(currentState.dir, xQuadNeg);
            break;
        case '+':
        case 'Z':
            currentState.dir = XMQuaternionMultiply(currentState.dir, zQuadPos);
            break;
        case '-':
        case 'z':
            currentState.dir = XMQuaternionMultiply(currentState.dir, zQuadNeg);
            break;
        case '|':
            currentState.dir = XMQuaternionMultiply(currentState.dir, xQuad180);
            break;
        case 'F':
        case 'L':
            prevPos = currentState.pos;

            {
                float cmdParam = 0.0f;  //GetParam(cmdIt, axiom.end());
                float len = _params.SegmentLength(&_params, cmdParam);
                axis = XMVector3TransformNormal(yVec, XMMatrixRotationQuaternion(currentState.dir));
                currentState.pos = currentState.pos + axis * len * (cmd == 'L' ? 0.5f : 1.0f);
            }

            XMStoreFloat4(&tmpPrev, prevPos);
            XMStoreFloat4(&tmpNext, currentState.pos);

            currentState.branch = AddBranch(currentState.branch, tmpPrev, tmpNext, (cmd == 'L' ? Leaf : Stick), _params.thickness);
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
        //case 'X':
        case ' ':
            break; // noop
        default:
            unknown.push_back(cmd);
            break;
        }

        pos++;
        done.push_back(cmd);
    }

    LOG(unknown.c_str());
}

