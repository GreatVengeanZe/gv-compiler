#include <vector>
#include <string>
#include <memory>

class SyntaxTreeNode
{
public:
    std::string value;
    std::vector<std::shared_ptr<SyntaxTreeNode>> children;

    SyntaxTreeNode(const std::string& val) : value(val) {}

    void addChild(const std::shared_ptr<SyntaxTreeNode>& child) {
        children.push_back(child);
    }
};

class SyntaxTree
{

public:
    SyntaxTree() : root(nullptr) {}

    void setRoot(const std::shared_ptr<SyntaxTreeNode>& node) {
        root = node;
    }

    std::shared_ptr<SyntaxTreeNode> getRoot() const {
        return root;
    }

private:
    std::shared_ptr<SyntaxTreeNode> root;
};
