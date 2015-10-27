#ifndef MEMGRAPH_CYPHER_TREE_PRINT_HPP
#define MEMGRAPH_CYPHER_TREE_PRINT_HPP

#include <iostream>
#include <stack>

#include "cypher/visitor/traverser.hpp"

class PrintVisitor : public Traverser
{
public:
    class Printer
    {
    public:
        Printer(std::ostream& stream, const std::string& header)
            : stream(stream)
        {
            stream << header;
        }

        ~Printer()
        {
            stream << std::endl;
        }

        class Entry
        {
        public:
            Entry(Printer& printer) : printer(printer), valid(true)
            {
                printer.level++;
    
                for(size_t i = 1; i < printer.level; ++i)
                    printer.stream << "|  ";

                printer.stream << "+--";
            }

            Entry(const Entry&) = delete;
            
            Entry(Entry&& other) : printer(other.printer), valid(true)
            {
                other.valid = false;
            }
            
            ~Entry()
            {
                if(valid)
                    printer.level--;
            }

            template <class T>
            friend Entry& operator<<(Entry& entry, const T& item)
            {
                entry.printer.stream << item;
                return entry;
            }

        private:
            Printer& printer;
            bool valid;
        };

        Entry advance()
        {
            stream << std::endl;
            return std::move(Entry(*this));
        }

        Entry advance(const std::string& text)
        {
            stream << std::endl;
            auto entry = Entry(*this);
            entry << text;
            return std::move(entry);
        }

    private:
        std::ostream& stream;
        size_t level = 0;
    };

    PrintVisitor(std::ostream& stream)
        : printer(stream, "Printing AST") {}

    void visit(ast::Start& start) override
    {
        auto entry = printer.advance("Start");
        Traverser::visit(start);
    }
    
    void visit(ast::ReadQuery& read_query) override
    {
        auto entry = printer.advance("Read Query");
        Traverser::visit(read_query);
    }

    void visit(ast::Match& match) override
    {
        auto entry = printer.advance("Match");
        Traverser::visit(match);
    }

    void visit(ast::Pattern& pattern) override
    {
        auto entry = printer.advance("Pattern");
        Traverser::visit(pattern);
    }

    void visit(ast::Node& node) override
    {
        auto entry = printer.advance("Node");
        Traverser::visit(node);
    }

    void visit(ast::Identifier& idn) override
    {
        auto entry = printer.advance();
        entry << "Identifier '" << idn.name << "'";
    }

    void visit(ast::Return& return_clause) override
    {
        auto entry = printer.advance("Return");
        Traverser::visit(return_clause);
    }

    void visit(ast::Accessor& accessor) override
    {
        auto entry = printer.advance("Accessor");
        Traverser::visit(accessor);
    }

    void visit(ast::Boolean& boolean) override
    {
        auto entry = printer.advance();
        entry << "Boolean " << boolean.value;
    }

    void visit(ast::Float& floating) override
    {
        auto entry = printer.advance();
        entry << "Float " << floating.value;
    }

    void visit(ast::Integer& integer) override
    {
        auto entry = printer.advance();
        entry << "Integer " << integer.value;
    }

    void visit(ast::String& string) override
    {
        auto entry = printer.advance();
        entry << "String " << string.value;
    }

    void visit(ast::Property& property) override
    {
        auto entry = printer.advance("Property");
        Traverser::visit(property);
    }

    void visit(ast::And& and_expr) override
    {
        auto entry = printer.advance("And");
        Traverser::visit(and_expr);
    }

    void visit(ast::Or& or_expr) override
    {
        auto entry = printer.advance("Or");
        Traverser::visit(or_expr);
    }

    void visit(ast::Lt& lt_expr) override
    {
        auto entry = printer.advance("Less Than");
        Traverser::visit(lt_expr);
    }

    void visit(ast::Gt& gt_expr) override
    {
        auto entry = printer.advance("Greater Than");
        Traverser::visit(gt_expr);
    }

    void visit(ast::Ge& ge_expr) override
    {
        auto entry = printer.advance("Greater od Equal");
        Traverser::visit(ge_expr);
    }

    void visit(ast::Le& le_expr) override
    {
        auto entry = printer.advance("Less or Equal");
        Traverser::visit(le_expr);
    }

    void visit(ast::Eq& eq_expr) override
    {
        auto entry = printer.advance("Equal");
        Traverser::visit(eq_expr);
    }

    void visit(ast::Ne& ne_expr) override
    {
        auto entry = printer.advance("Not Equal");
        Traverser::visit(ne_expr);
    }

    void visit(ast::Plus& plus) override
    {
        auto entry = printer.advance("Plus");
        Traverser::visit(plus);
    }

    void visit(ast::Minus& minus) override
    {
        auto entry = printer.advance("Minus");
        Traverser::visit(minus);
    }

    void visit(ast::Star& star) override
    {
        auto entry = printer.advance("Star");
        Traverser::visit(star);
    }

    void visit(ast::Slash& slash) override
    {
        auto entry = printer.advance("Slash");
        Traverser::visit(slash);
    }

    void visit(ast::Rem& rem) override
    {
        auto entry = printer.advance("Rem (%)");
        Traverser::visit(rem);
    }

    void visit(ast::PropertyList& prop_list) override
    {
        auto entry = printer.advance("Property List");
        Traverser::visit(prop_list);
    }

    void visit(ast::RelationshipList& rel_list) override
    {
        auto entry = printer.advance("Relationship List");
        Traverser::visit(rel_list);
    }

    void visit(ast::Relationship& rel) override
    {
        auto entry = printer.advance("Relationship");
        entry << " direction: " << rel.direction;
        Traverser::visit(rel);
    }

    void visit(ast::RelationshipSpecs& rel_specs) override
    {
        auto entry = printer.advance("Relationship Specs");
        Traverser::visit(rel_specs);
    }

    void visit(ast::LabelList& labels) override
    {
        auto entry = printer.advance("Label List");
        Traverser::visit(labels);
    }

    void visit(ast::ReturnList& return_list) override
    {
        auto entry = printer.advance("Return List");
        Traverser::visit(return_list);
    }

    void visit(ast::Where& where) override
    {
        auto entry = printer.advance("Where");
        Traverser::visit(where);
    }

    void visit(ast::WriteQuery& write_query) override
    {
        auto entry = printer.advance("Write Query");
        Traverser::visit(write_query);
    }

    void visit(ast::Create& create) override
    {
        auto entry = printer.advance("Create");
        Traverser::visit(create);
    }

private:
    Printer printer;
};

#endif
