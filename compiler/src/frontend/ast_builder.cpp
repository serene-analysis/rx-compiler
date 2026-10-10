#include "ast_builder.h"

std::any ASTVisitor::visitCrate(RxParser::CrateContext *ctx) {
    auto now = make<Crate>();
    auto items = ctx->item();
    for (auto dw : items) {
        now->items_.push_back(anyItem(visit(dw)));
    }
    return static_cast<Crate*>(now);
}

std::any ASTVisitor::visitItem(RxParser::ItemContext *ctx) {
    auto func = ctx->functionDefinition();
    auto stru = ctx->structDefinition();
    auto cons = ctx->constantItem();
    auto impl = ctx->inherentImpl();
    if (func) {
        return visit(func);
    }
    if (stru) {
        return visit(stru);
    }
    if (cons) {
        return visit(cons);
    }
    if (impl) {
        return visit(impl);
    }
    return static_cast<Item*>(nullptr);
}

std::any ASTVisitor::visitUseDeclaration(RxParser::UseDeclarationContext *ctx) {
    // Do nothing, use is discarded.
    throw "visitUseDeclaration";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitUseTree(RxParser::UseTreeContext *ctx) {
    // Do nothing, use is discarded.
    throw "visitUseTree";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitUsePath(RxParser::UsePathContext *ctx) {
    // Do nothing, use is discarded.
    throw "visitUsePath";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitUsePathSegment(RxParser::UsePathSegmentContext *ctx) {
    // Do nothing, use is discarded.
    throw "visitUsePathSegment";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitFunctionDefinition(RxParser::FunctionDefinitionContext *ctx) {
    auto now = make<funcItem>();
    now->name_ = ctx->identifier()->getText();
    if (auto* params = ctx->functionParameters())
        now->parameter_ = std::any_cast<std::vector<Param>>(visit(params));
    if (auto* ret = ctx->typeRef())
        now->retType_ = anyType(visit(ret));
    else {
        auto* unit = make<Type>();
        unit->kind_ = Type::UNIT;
        now->retType_ = unit;
    }
    now->body_ = anyExpr(visit(ctx->blockExpression()));
    return static_cast<Item*>(now);
}
/*
functionDefinition
    : FN identifier genericParams? LPAREN functionParameters? RPAREN
      (ARROW typeRef)? whereClause? blockExpression
    ;
*/

std::any ASTVisitor::visitFunctionParameters(RxParser::FunctionParametersContext *ctx) {
    std::vector<Param> ret;
    if (auto self = ctx->selfParam(); self) {
        ret.push_back(std::any_cast<Param>(visit(self)));
    }
    auto func = ctx->functionParam();
    for (auto now : func) {
        ret.push_back(std::any_cast<Param>(visit(now)));
    }
    return ret;
}
/*
functionParameters
    : selfParam (COMMA functionParam)* COMMA?
    | functionParam (COMMA functionParam)* COMMA?
    ;
*/

std::any ASTVisitor::visitSelfParam(RxParser::SelfParamContext *ctx) {
    Param ret;
    ret.name_ = "self";
    ret.isSelf_ = true;
    if(ctx->MUT()){
        ret.isMut_ = true;
    }
    if(ctx->AMP()){
        auto ref = make<Type>();
        ref->kind_ = Type::kTypeKind::REF;
        ref->isMut_ = ret.isMut_;
        ref->elem_ = make<Type>();
        ref->elem_->kind_ = Type::kTypeKind::STRUCT;
        ref->elem_->name_ = "Self";
        ret.type_ = ref;
    }
    else {
        auto tp = make<Type>();
        tp->kind_ = Type::kTypeKind::STRUCT;
        tp->name_ = "Self";
        ret.type_ = tp;
    }
    return ret;
}
/*
selfParam
    : (AMP lifetime?)? MUT? SELF_VALUE
    ;
*/

std::any ASTVisitor::visitFunctionParam(RxParser::FunctionParamContext *ctx) {
    Param ret;
    ret.name_ = ctx->identifierBinding()->identifier()->getText();
    ret.isMut_ = (ctx->identifierBinding()->MUT() != nullptr);
    ret.isSelf_ = false;
    ret.type_ = anyType(visit(ctx->typeRef()));
    return ret;
}
/*
functionParam
    : identifierBinding COLON typeRef
    ;
*/

std::any ASTVisitor::visitStructDefinition(RxParser::StructDefinitionContext *ctx) {
    auto now = make<structItem>();
    auto outer = ctx->outerAttribute();
    for (auto attr : outer) {
        auto derives = attr->deriveName();
        for (auto der : derives) {
            now->derives_.push_back(der->getText());
        }
    }
    now->name_ = ctx->identifier()->getText();
    auto fields = ctx->structField();
    for (auto field : fields) {
        structField cur;
        cur.name_ = field->identifier()->getText();
        cur.type_ = anyType(visit(field->typeRef()));
        now->fields_.push_back(cur);
    }
    return static_cast<Item*>(now);
}
/*
structDefinition
    : outerAttribute* STRUCT identifier genericParams? whereClause?
      LBRACE (structField (COMMA structField)* COMMA?)? RBRACE
    ;

structField
    : identifier COLON typeRef
    ;

outerAttribute
    : HASH LBRACKET DERIVE LPAREN (deriveName (COMMA deriveName)* COMMA?)?
      RPAREN RBRACKET
    ;

deriveName
    : COPY | CLONE | PARTIAL_EQ | EQ
    ;
*/

std::any ASTVisitor::visitStructField(RxParser::StructFieldContext *ctx) {
    // Compressed
    throw "visitStructField";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitOuterAttribute(RxParser::OuterAttributeContext *ctx) {
    // Compressed
    throw "visitOuterAttribute";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitDeriveName(RxParser::DeriveNameContext *ctx) {
    // Compressed
    throw "visitDeriveName";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitConstantItem(RxParser::ConstantItemContext *ctx) {
    auto now = make<constItem>();
    now->name_ = ctx->identifier()->getText();
    now->type_ = anyType(visit(ctx->typeRef()));
    now->value_ = anyConstValue(visit(ctx->constValue()));
    return static_cast<Item*>(now);
}
/*
constantItem
    : CONST identifier COLON typeRef equalsSign constValue SEMI
    ;
*/

std::any ASTVisitor::visitInherentImpl(RxParser::InherentImplContext *ctx) {
    auto now = make<ImplItem>();
    now->type_ = anyType(visit(ctx->typeRef()));
    auto items = ctx->associatedItem();
    for (auto item : items) {
        now->items_.push_back(anyItem(visit(item)));
    }
    return static_cast<Item*>(now);
}
/*
inherentImpl
    : IMPL genericParams? typeRef whereClause? LBRACE associatedItem* RBRACE
    ;

associatedItem
    : constantItem | functionDefinition
    ;
*/

std::any ASTVisitor::visitAssociatedItem(RxParser::AssociatedItemContext *ctx) {
    if (auto* fn = ctx->functionDefinition()) return visit(fn);
    if (auto* c = ctx->constantItem()) return visit(c);
    return static_cast<Item*>(nullptr);
}

std::any ASTVisitor::visitGenericParams(RxParser::GenericParamsContext *ctx) {
    // Do nothing, dont need to consider
    throw "visitGenericParams";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitLifetimeParam(RxParser::LifetimeParamContext *ctx) {
    // Do nothing, dont need to consider
    throw "visitLifetimeParam";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitLifetime(RxParser::LifetimeContext *ctx) {
    // Do nothing, dont need to consider
    throw "visitLifetime";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitLifetimeBounds(RxParser::LifetimeBoundsContext *ctx) {
    // Do nothing, dont need to consider
    throw "visitLifetimeBounds";
    return visitChildren(ctx);
}

// 10.3 morning

std::any ASTVisitor::visitTypeParamBounds(RxParser::TypeParamBoundsContext *ctx) {
    // Do nothing, dont need to consider
    throw "visitTypeParamBounds";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitWhereClause(RxParser::WhereClauseContext *ctx) {
    // Do nothing, dont need to consider
    throw "visitWhereClause";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitWhereClauseItem(RxParser::WhereClauseItemContext *ctx) {
    // Do nothing, dont need to consider
    throw "visitWhereClauseItem";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitTypeRef(RxParser::TypeRefContext *ctx) {
    if (ctx->typeRef()) {
        return anyType(visit(ctx->typeRef()));
    }
    if (ctx->LPAREN()) {
        auto now = make<Type>();
        now->kind_ = Type::kTypeKind::UNIT;
        now->name_ = "()";
        return static_cast<Type*>(now);
    }
    if (ctx->typePath()) {
        return anyType(visit(ctx->typePath()));
    }
    if (ctx->referenceType()) {
        return anyType(visit(ctx->referenceType()));
    }
    if (ctx->arrayType()) {
        return anyType(visit(ctx->arrayType()));
    }
    throw "visitTypeRef";
    return visitChildren(ctx);
}
/*
typeRef
    : LPAREN typeRef RPAREN
    | LPAREN RPAREN
    | typePath
    | referenceType
    | arrayType
    ;
*/

std::any ASTVisitor::visitReferenceType(RxParser::ReferenceTypeContext *ctx) {
    bool isMut = (ctx->MUT() != nullptr);
    Type* inner = anyType(visit(ctx->typeRef()));
    auto refMake = [&](bool isMut, Type* elem) {
        auto ref = make<Type>();
        ref->kind_ = Type::kTypeKind::REF;
        ref->isMut_ = isMut;
        ref->elem_ = elem;
        return ref;
    };
    if (ctx->ANDAND()) {
        return static_cast<Type*>(refMake(false, refMake(isMut, inner)));
    }
    return static_cast<Type*>(refMake(isMut, inner));
}
/*
referenceType
    // ANDAND constructs TWO references; lifetime/MUT belong to the inner one.
    : (AMP | ANDAND) lifetime? MUT? typeRef
    ;
*/

std::any ASTVisitor::visitArrayType(RxParser::ArrayTypeContext *ctx) {
    auto now = make<Type>();
    now->kind_ = Type::kTypeKind::ARRAY;
    now->name_ = "[k, N]";
    now->elem_ = anyType(visit(ctx->typeRef()));
    now->arrayLength_ = anyConstValue(visit(ctx->constValue()));
    return static_cast<Type*>(now);
}
/*
arrayType
    : LBRACKET typeRef SEMI constValue RBRACKET
    ;
*/

std::any ASTVisitor::visitTypePath(RxParser::TypePathContext *ctx) {
    std::string name;
    std::vector<Type*> types;
    auto segs = ctx->typePathSegment();
    for (auto seg : segs) {
        if (auto ident = seg->pathIdentSegment(); ident) name = ident->getText();
        auto args = seg->genericArgs();
        if (args) {
            auto arg = args->genericArg();
            for (auto elem : arg) {
                if (elem->typeRef()) {
                    types.push_back(anyType(visit(elem->typeRef())));
                }
            }
        }
    }
    return makeNamedType(name, types);
}
/*
typePath
    : typePathSegment (PATHSEP typePathSegment)*
    ;

typePathSegment
    : pathIdentSegment (PATHSEP? genericArgs)?
    ;

pathIdentSegment
    : identifier | SELF_VALUE | SELF_TYPE
    ;

genericArgs
    : LT (genericArg (COMMA genericArg)* COMMA?)? genericClose
    ;

genericArg
    : lifetime | typeRef
    ;

genericClose
    : GT | GT_SECOND
    ;
*/

std::any ASTVisitor::visitTypePathSegment(RxParser::TypePathSegmentContext *ctx) {
    // Compressed
    throw "visitTypePathSegment";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitPathInExpression(RxParser::PathInExpressionContext *ctx) {
    auto now = make<pathExpr>();
    auto segs = ctx->pathExprSegment();
    for (auto seg : segs) {
        pathSeg s;
        s.name_ = seg->pathIdentSegment()->getText();
        if (auto args = seg->genericArgs(); args) {
            auto arg = args->genericArg();
            for (auto cur : arg) {
                if (cur->typeRef()) {
                    s.types_.push_back(anyType(visit(cur->typeRef())));
                }
            }
        }
        now->path_.push_back(s);
    }
    return static_cast<Expr*>(now);
}
/*
pathInExpression
    : pathExprSegment (PATHSEP pathExprSegment)*
    ;

pathExprSegment
    : pathIdentSegment (PATHSEP genericArgs)?
    ;
*/

std::any ASTVisitor::visitPathExprSegment(RxParser::PathExprSegmentContext *ctx) {
    // Compressed
    throw "visitPathExprSegment";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitPathIdentSegment(RxParser::PathIdentSegmentContext *ctx) {
    // Compressed
    throw "visitPathIdentSegment";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitGenericArgs(RxParser::GenericArgsContext *ctx) {
    // Compressed
    throw "visitGenericArgs";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitGenericArg(RxParser::GenericArgContext *ctx) {
    // Compressed
    throw "visitGenericArg";
    return visitChildren(ctx);
}

// 2026.10.3 night

std::any ASTVisitor::visitGenericClose(RxParser::GenericCloseContext *ctx) {
    // Do nothing, just to conclude Vec<Box<i32>> which >> would be recognised as >>.
    return visitChildren(ctx);
}

std::any ASTVisitor::visitClosedCastType(RxParser::ClosedCastTypeContext *ctx) {
    if (ctx->typeRef()) {
        return anyType(visit(ctx->typeRef()));
    }
    if (ctx->LPAREN()) {
        auto now = make<Type>();
        now->kind_ = Type::kTypeKind::UNIT;
        now->name_ = "()";
        return static_cast<Type*>(now);
    }
    if (ctx->arrayType()) {
        return anyType(visit(ctx->arrayType()));
    }
    if (ctx->AMP() || ctx->ANDAND()) {
        bool isMut = (ctx->MUT() != nullptr);
        Type* inner = anyType(visit(ctx->closedCastType()));
        auto refMake = [&](bool isMut, Type* elem) {
            auto ref = make<Type>();
            ref->kind_ = Type::kTypeKind::REF;
            ref->isMut_ = isMut;
            ref->elem_ = elem;
            return ref;
        };
        if (ctx->ANDAND()) {
            return static_cast<Type*>(refMake(false, refMake(isMut, inner)));
        }
        return static_cast<Type*>(refMake(isMut, inner));
    }
    std::string name = ctx->pathIdentSegment() ? ctx->pathIdentSegment()->getText() : "";
    std::vector<Type*> types;
    if (auto args = ctx->genericArgs(); args) {
        auto arg = args->genericArg();
        for (auto cur : arg) {
            if (cur->typeRef()) {
                types.push_back(anyType(visit(cur->typeRef())));
            }
        }
    }
    return makeNamedType(name, types);
}
/*
closedCastType
    : LPAREN typeRef? RPAREN
    | arrayType
    | (AMP | ANDAND) lifetime? MUT? closedCastType
    | (typePathSegment PATHSEP)* pathIdentSegment PATHSEP? genericArgs
    ;
*/

std::any ASTVisitor::visitConstValue(RxParser::ConstValueContext *ctx) {
    if (ctx->LPAREN()) {
        return anyConstValue(visit(ctx->constValue()));
    }
    auto now = make<ConstValue>();
    if (ctx->INTEGER_LITERAL()) {
        now->kind_ = ConstValue::kConstKind::INT;
        now->text_ = ctx->INTEGER_LITERAL()->getText();
    }
    else if (ctx->TRUE() || ctx->FALSE()) {
        now->kind_ = ConstValue::kConstKind::BOOL;
        now->text_ = ctx->TRUE() ? "TRUE" : "FALSE";
    }
    else if (auto path = ctx->pathInExpression(); path) {
        now->kind_ = ConstValue::kConstKind::PATH;
        now->text_ = ctx->pathInExpression()->getText();
    }
    else if (auto mag = ctx->magnitude(); mag) {
        now->kind_ = ConstValue::kConstKind::NEG;
        now->inner_ = anyConstValue(visit(mag));
    }
    return static_cast<ConstValue*>(now);
}
/*
constValue
    : INTEGER_LITERAL
    | TRUE
    | FALSE
    | pathInExpression
    | MINUS magnitude
    | LPAREN constValue RPAREN
    ;
*/

std::any ASTVisitor::visitMagnitude(RxParser::MagnitudeContext *ctx) {
    if (ctx->LPAREN()) {
        return anyConstValue(visit(ctx->magnitude()));
    }
    auto now = make<ConstValue>();
    if (ctx->INTEGER_LITERAL()) {
        now->kind_ = ConstValue::kConstKind::INT;
        now->text_ = ctx->INTEGER_LITERAL()->getText();
    }
    else if (auto path = ctx->pathInExpression(); path) {
        now->kind_ = ConstValue::kConstKind::PATH;
        now->text_ = ctx->pathInExpression()->getText();
    }
    return static_cast<ConstValue*>(now);
}
/*
magnitude
    : INTEGER_LITERAL | pathInExpression | LPAREN magnitude RPAREN
    ;
*/

std::any ASTVisitor::visitIdentifierBinding(RxParser::IdentifierBindingContext *ctx) {
    // Compressed
    throw "visitIdentifierBinding";
    return visitChildren(ctx);
}

std::any ASTVisitor::visitLetStatement(RxParser::LetStatementContext *ctx) {
    auto now = make<letStmt>();
    now->isMut_ = (ctx->identifierBinding()->MUT() != nullptr);
    now->name_ = ctx->identifierBinding()->identifier()->getText();
    if (ctx->typeRef()) {
        now->type_ = anyType(visit(ctx->typeRef()));
    }
    now->init_ = anyExpr(visit(ctx->expression()));
    return static_cast<Stmt*>(now);
}
/*
letStatement
    : LET identifierBinding (COLON typeRef)? equalsSign expression SEMI
    ;
*/

std::any ASTVisitor::visitBlockExpression(RxParser::BlockExpressionContext *ctx) {
    auto now = make<blockExpr>();
    auto stmts = ctx->statement();
    for (auto stmt : stmts) {
        if (stmt != *std::prev(stmts.end())) {
            now->stmts_.push_back(anyStmt(visit(stmt)));
        }
        else {
            bool last_value = false;
            if (auto blk = stmt->expressionWithBlock(); blk) {
                if (ctx->statementExpression() == nullptr && stmt->SEMI() == nullptr) {
                    last_value = true;
                }
            }
            if (last_value) {
                now->tail_ = anyExpr(visit(stmt->expressionWithBlock()));
            }
            else {
                now->stmts_.push_back(anyStmt(visit(stmt)));
            }
        }
    }
    if(ctx->statementExpression()) {
        now->tail_ = anyExpr(visit(ctx->statementExpression()));
    }
    return static_cast<Expr*>(now);
}
/*
blockExpression
    : LBRACE statement* statementExpression? RBRACE
    ;
*/

std::any ASTVisitor::visitStatement(RxParser::StatementContext *ctx) {
    if (ctx->letStatement()) {
        return anyStmt(visit(ctx->letStatement()));
    }
    if (ctx->expressionWithBlock()) {
        auto now = make<exprStmt>();
        now->expr_ = anyExpr(visit(ctx->expressionWithBlock()));
        return static_cast<Stmt*>(now);
    }
    if (ctx->statementExpression()) {
        auto now = make<exprStmt>();
        now->expr_ = anyExpr(visit(ctx->statementExpression()));
        return static_cast<Stmt*>(now);
    }
    return static_cast<Stmt*>(nullptr);
}
/*
statement
    : SEMI
    | letStatement
    | expressionWithBlock SEMI?
    | statementExpression SEMI
    ;
*/

std::any ASTVisitor::visitExpressionWithBlock(RxParser::ExpressionWithBlockContext *ctx) {
    if (ctx->LOOP()) {
        auto now = make<loopExpr>();
        now->block_ = static_cast<blockExpr*>(anyExpr(visit(ctx->blockExpression())));
        return static_cast<Expr*>(now);
    }
    if (ctx->WHILE()) {
        auto now = make<whileExpr>();
        now->cond_ = anyExpr(visit(ctx->conditionExpression()));
        now->block_ = static_cast<blockExpr*>(anyExpr(visit(ctx->blockExpression())));
        return static_cast<Expr*>(now);
    }
    if (auto f = ctx->ifExpression()) {
        return anyExpr(visit(f));
    }
    if (auto blk = ctx->blockExpression()) {
        return anyExpr(visit(blk));
    }
    return visitChildren(ctx);
}
/*
expressionWithBlock
    : blockExpression
    | ifExpression
    | LOOP blockExpression
    | WHILE conditionExpression blockExpression
    ;
*/

std::any ASTVisitor::visitIfExpression(RxParser::IfExpressionContext *ctx) {
    auto now = make<ifExpr>();
    now->cond_ = anyExpr(visit(ctx->conditionExpression()));
    now->then_ = static_cast<blockExpr*>(anyExpr(visit(ctx->blockExpression(0))));
    if (ctx->ELSE()) {
        if (ctx->blockExpression().size() == 2u) {
            now->else_ = anyExpr(visit(ctx->blockExpression(1)));
        }
        else {
            now->else_ = anyExpr(visit(ctx->ifExpression()));
        }
    }
    return static_cast<Expr*>(now);
}
/*
ifExpression
    : IF conditionExpression blockExpression
      (ELSE (blockExpression | ifExpression))?
    ;
*/

std::any ASTVisitor::visitExpression(RxParser::ExpressionContext *ctx) {
    return anyExpr(visit(ctx->assignmentExpression()));
}
/*
expression
    : assignmentExpression
    ;
*/

std::any ASTVisitor::visitAssignmentExpression(RxParser::AssignmentExpressionContext *ctx) {
    Expr* lhs = anyExpr(visit(ctx->logicalOrExpression()));
    if (!ctx->assignmentOperator()) {
        return static_cast<Expr*>(lhs);
    }
    auto now = make<binaryExpr>();
    now->op_ = ctx->assignmentOperator()->getText();
    now->lhs_ = lhs;
    now->rhs_ = anyExpr(visit(ctx->expression()));
    return static_cast<Expr*>(now);
}
/*
assignmentExpression
    : logicalOrExpression (assignmentOperator expression)?
    ;
*/

std::any ASTVisitor::visitLogicalOrExpression(RxParser::LogicalOrExpressionContext *ctx) {
    auto operands = ctx->logicalAndExpression();
    auto ops = ctx->OROR();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
logicalOrExpression
    : logicalAndExpression (OROR logicalAndExpression)*
    ;
*/

std::any ASTVisitor::visitLogicalAndExpression(RxParser::LogicalAndExpressionContext *ctx) {
    auto operands = ctx->comparisonExpression();
    auto ops = ctx->ANDAND();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
logicalAndExpression
    : comparisonExpression (ANDAND comparisonExpression)*
    ;
*/

std::any ASTVisitor::visitComparisonExpression(RxParser::ComparisonExpressionContext *ctx) {
    if (ctx->LT()) {
        auto now = make<binaryExpr>();
        now->op_ = ctx->LT()->getText();
        now->lhs_ = anyExpr(visit(ctx->closedBitOrExpression()));
        now->rhs_ = anyExpr(visit(ctx->bitOrExpression(0)));
        return static_cast<Expr*>(now);
    }
    auto lhs = anyExpr(visit(ctx->bitOrExpression(0)));
    if (!ctx->comparisonExceptLt()) {
        return static_cast<Expr*>(lhs);
    }
    auto now = make<binaryExpr>();
    now->op_ = ctx->comparisonExceptLt()->getText();
    now->lhs_ = lhs;
    now->rhs_ = anyExpr(visit(ctx->bitOrExpression(1)));
    return static_cast<Expr*>(now);
}
/*
comparisonExpression
    : bitOrExpression (comparisonExceptLt bitOrExpression)?
    | closedBitOrExpression LT bitOrExpression
    ;
*/

std::any ASTVisitor::visitBitOrExpression(RxParser::BitOrExpressionContext *ctx) {
    auto operands = ctx->bitXorExpression();
    auto ops = ctx->PIPE();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
bitOrExpression
    : bitXorExpression (PIPE bitXorExpression)*
    ;
*/

std::any ASTVisitor::visitClosedBitOrExpression(RxParser::ClosedBitOrExpressionContext *ctx) {
    if (ctx->PIPE().size() == 0u) {
        return anyExpr(visit(ctx->closedBitXorExpression()));
    }
    auto operands = ctx->bitXorExpression();
    auto ops = ctx->PIPE();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedBitXorExpression()));
    return static_cast<Expr*>(now);
}
/*
closedBitOrExpression
    : (bitXorExpression PIPE)* closedBitXorExpression
    ;
*/

std::any ASTVisitor::visitBitXorExpression(RxParser::BitXorExpressionContext *ctx) {
    auto operands = ctx->bitAndExpression();
    auto ops = ctx->CARET();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
bitXorExpression
    : bitAndExpression (CARET bitAndExpression)*
    ;
*/

std::any ASTVisitor::visitClosedBitXorExpression(RxParser::ClosedBitXorExpressionContext *ctx) {
    if (ctx->CARET().size() == 0u) {
        return anyExpr(visit(ctx->closedBitAndExpression()));
    }
    auto operands = ctx->bitAndExpression();
    auto ops = ctx->CARET();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedBitAndExpression()));
    return static_cast<Expr*>(now);
}
/*
closedBitXorExpression
    : (bitAndExpression CARET)* closedBitAndExpression
    ;
*/

std::any ASTVisitor::visitBitAndExpression(RxParser::BitAndExpressionContext *ctx) {
    auto operands = ctx->shiftExpression();
    auto ops = ctx->AMP();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
bitAndExpression
    : shiftExpression (AMP shiftExpression)*
    ;
*/

std::any ASTVisitor::visitClosedBitAndExpression(RxParser::ClosedBitAndExpressionContext *ctx) {
    if (ctx->AMP().size() == 0u) {
        return anyExpr(visit(ctx->closedShiftExpression()));
    }
    auto operands = ctx->shiftExpression();
    auto ops = ctx->AMP();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedShiftExpression()));
    return static_cast<Expr*>(now);
}
/*
closedBitAndExpression
    : (shiftExpression AMP)* closedShiftExpression
    ;
*/

std::any ASTVisitor::visitShiftExpression(RxParser::ShiftExpressionContext *ctx) {
    if (ctx->SHL().size() != 0u) {
        auto operands = ctx->closedAdditiveExpression();
        auto ops = ctx->SHL();
        Expr* left = anyExpr(visit(operands[0]));
        for (int i = 0; i < ops.size() - 1; i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i + 1]));
            left = now;
        }
        auto now = make<binaryExpr>();
        now->op_ = ops[ops.size() - 1]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(ctx->additiveExpression(0)));
        return static_cast<Expr*>(now);
    }
    else {
        auto operands = ctx->additiveExpression();
        auto ops = ctx->shiftRight();
        Expr* left = anyExpr(visit(operands[0]));
        for (int i = 0; i < ops.size(); i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i + 1]));
            left = now;
        }
        return static_cast<Expr*>(left);
    }
}
/*
shiftExpression
    : (closedAdditiveExpression SHL | additiveExpression shiftRight)* additiveExpression
    ;
*/

std::any ASTVisitor::visitClosedShiftExpression(RxParser::ClosedShiftExpressionContext *ctx) {
    if (ctx->shiftRight().size() != 0u) {
        auto operands = ctx->additiveExpression();
        auto ops = ctx->shiftRight();
        Expr* left = anyExpr(visit(operands[0]));
        for (int i = 0; i < ops.size() - 1; i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i + 1]));
            left = now;
        }
        auto now = make<binaryExpr>();
        now->op_ = ops[ops.size() - 1]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(ctx->closedAdditiveExpression(0)));
        return static_cast<Expr*>(now);
    }
    else {
        auto operands = ctx->closedAdditiveExpression();
        auto ops = ctx->SHL();
        Expr* left = anyExpr(visit(operands[0]));
        for (int i = 0; i < ops.size(); i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i + 1]));
            left = now;
        }
        return static_cast<Expr*>(left);
    }
}
/*
closedShiftExpression
    : (closedAdditiveExpression SHL | additiveExpression shiftRight)* closedAdditiveExpression
    ;
*/

std::any ASTVisitor::visitAdditiveExpression(RxParser::AdditiveExpressionContext *ctx) {
    auto operands = ctx->multiplicativeExpression();
    auto ops = ctx->additiveOperator();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
additiveExpression
    : multiplicativeExpression (additiveOperator multiplicativeExpression)*
    ;
*/

std::any ASTVisitor::visitClosedAdditiveExpression(RxParser::ClosedAdditiveExpressionContext *ctx) {
    if (ctx->additiveOperator().size() == 0u) {
        return anyExpr(visit(ctx->closedMultiplicativeExpression()));
    }
    auto operands = ctx->multiplicativeExpression();
    auto ops = ctx->additiveOperator();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedMultiplicativeExpression()));
    return static_cast<Expr*>(now);
}
/*
closedAdditiveExpression
    : (multiplicativeExpression additiveOperator)* closedMultiplicativeExpression
    ;
*/

std::any ASTVisitor::visitMultiplicativeExpression(RxParser::MultiplicativeExpressionContext *ctx) {
    auto operands = ctx->castExpression();
    auto ops = ctx->multiplicativeOperator();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
multiplicativeExpression
    : castExpression (multiplicativeOperator castExpression)*
    ;
*/

std::any ASTVisitor::visitClosedMultiplicativeExpression(RxParser::ClosedMultiplicativeExpressionContext *ctx) {
    if (ctx->multiplicativeOperator().size() == 0u) {
        return anyExpr(visit(ctx->closedCastExpression()));
    }
    auto operands = ctx->castExpression();
    auto ops = ctx->multiplicativeOperator();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedCastExpression()));
    return static_cast<Expr*>(now);
}
/*
closedMultiplicativeExpression
    : (castExpression multiplicativeOperator)* closedCastExpression
    ;
*/

std::any ASTVisitor::visitCastExpression(RxParser::CastExpressionContext *ctx) {
    Expr* now = anyExpr(visit(ctx->unaryExpression()));
    auto types = ctx->typeRef();
    for (auto cas : types) {
        auto cast = make<castExpr>();
        cast->from_ = now;
        cast->to_ = anyType(visit(cas));
        now = cast;
    }
    return static_cast<Expr*>(now);
}

std::any ASTVisitor::visitClosedCastExpression(RxParser::ClosedCastExpressionContext *ctx) {
    if (auto un = ctx->unaryExpression(); un) { return anyExpr(visit(un)); }
    Expr* from = anyExpr(visit(ctx->castExpression()));
    auto now = make<castExpr>();
    now->from_ = from;
    now->to_ = anyType(visit(ctx->closedCastType()));
    return static_cast<Expr*>(now);
}
/*
closedCastExpression
    : unaryExpression
    | castExpression AS closedCastType
    ;
*/

std::any ASTVisitor::visitUnaryExpression(RxParser::UnaryExpressionContext *ctx) {
    if (auto un = ctx->unaryExpression()) {
        std::string op = ctx->unaryOperator()->getText();
        Expr* operand = anyExpr(visit(un));
        if (op == "*") {
            auto now = make<derefExpr>();
            now->operand_ = operand;
            return static_cast<Expr*>(now);
        }
        else if (!op.empty() && op[0] == '&') {
            bool isMut = op.find("mut") != std::string::npos;
            int n = (op.size() > 1 && op[1] == '&') ? 2 : 1;
            auto refMake = [&](bool isMut, Expr* value) {
                auto ref = make<refExpr>();
                ref->isMut_ = isMut;
                ref->operand_ = value;
                return static_cast<Expr*>(ref);
            };
            if (n == 1) {
                return static_cast<Expr*>(refMake(isMut, operand));
            }
            else {
                return static_cast<Expr*>(refMake(false, static_cast<Expr*>(refMake(isMut, operand))));
            }
        }
        else {
            auto now = make<unaryExpr>();
            now->op_ = op;
            now->operand_ = operand;
            return static_cast<Expr*>(now);
        }
    }
    return anyExpr(visit(ctx->postfixExpression()));
}
/*
unaryExpression
    : unaryOperator unaryExpression
    | postfixExpression
    ;
*/

std::any ASTVisitor::visitPostfixExpression(RxParser::PostfixExpressionContext *ctx) {
    Expr* now = anyExpr(visit(ctx->primaryExpression()));
    return static_cast<Expr*>(appendSuffices(now, ctx->postfixSuffix()));
}
/*
postfixExpression
    : primaryExpression postfixSuffix*
    ;
*/

std::any ASTVisitor::visitConditionExpression(RxParser::ConditionExpressionContext *ctx) {
    return anyExpr(visit(ctx->conditionAssignmentExpression()));
}
/*
conditionExpression
    : conditionAssignmentExpression
*/

std::any ASTVisitor::visitConditionAssignmentExpression(RxParser::ConditionAssignmentExpressionContext *ctx) {
    Expr* lhs = anyExpr(visit(ctx->conditionLogicalOrExpression()));
    if (!ctx->assignmentOperator()) {
        return static_cast<Expr*>(lhs);
    }
    auto now = make<binaryExpr>();
    now->op_ = ctx->assignmentOperator()->getText();
    now->lhs_ = lhs;
    now->rhs_ = anyExpr(visit(ctx->conditionExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionAssignmentExpression
    : conditionLogicalOrExpression (assignmentOperator conditionExpression)?
    ;
*/

std::any ASTVisitor::visitConditionLogicalOrExpression(RxParser::ConditionLogicalOrExpressionContext *ctx) {
    auto operands = ctx->conditionLogicalAndExpression();
    auto ops = ctx->OROR();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionLogicalOrExpression
    : conditionLogicalAndExpression (OROR conditionLogicalAndExpression)*
    ;
*/

std::any ASTVisitor::visitConditionLogicalAndExpression(RxParser::ConditionLogicalAndExpressionContext *ctx) {
    auto operands = ctx->conditionComparisonExpression();
    auto ops = ctx->ANDAND();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionLogicalAndExpression
    : conditionComparisonExpression (ANDAND conditionComparisonExpression)*
    ;
*/

std::any ASTVisitor::visitConditionComparisonExpression(RxParser::ConditionComparisonExpressionContext *ctx) {
    if (ctx->LT()) {
        auto now = make<binaryExpr>();
        now->op_ = ctx->LT()->getText();
        now->lhs_ = anyExpr(visit(ctx->conditionClosedBitOrExpression()));
        now->rhs_ = anyExpr(visit(ctx->conditionBitOrExpression(0)));
        return static_cast<Expr*>(now);
    }
    auto lhs = anyExpr(visit(ctx->conditionBitOrExpression(0)));
    if (!ctx->comparisonExceptLt()) {
        return static_cast<Expr*>(lhs);
    }
    auto now = make<binaryExpr>();
    now->op_ = ctx->comparisonExceptLt()->getText();
    now->lhs_ = lhs;
    now->rhs_ = anyExpr(visit(ctx->conditionBitOrExpression(1)));
    return static_cast<Expr*>(now);
}
/*
conditionComparisonExpression
    : conditionBitOrExpression (comparisonExceptLt conditionBitOrExpression)?
    | conditionClosedBitOrExpression LT conditionBitOrExpression
    ;
*/

std::any ASTVisitor::visitConditionBitOrExpression(RxParser::ConditionBitOrExpressionContext *ctx) {
    auto operands = ctx->conditionBitXorExpression();
    auto ops = ctx->PIPE();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBitOrExpression
    : conditionBitXorExpression (PIPE conditionBitXorExpression)*
*/

std::any ASTVisitor::visitConditionClosedBitOrExpression(RxParser::ConditionClosedBitOrExpressionContext *ctx) {
    if (ctx->PIPE().size() == 0u) {
        return anyExpr(visit(ctx->conditionClosedBitXorExpression()));
    }
    auto operands = ctx->conditionBitXorExpression();
    auto ops = ctx->PIPE();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedBitXorExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionClosedBitOrExpression
    : (conditionBitXorExpression PIPE)* conditionClosedBitXorExpression
    ;
*/

std::any ASTVisitor::visitConditionBitXorExpression(RxParser::ConditionBitXorExpressionContext *ctx) {
    auto operands = ctx->conditionBitAndExpression();
    auto ops = ctx->CARET();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBitXorExpression
    : conditionBitAndExpression (CARET conditionBitAndExpression)*
    ;
*/

std::any ASTVisitor::visitConditionClosedBitXorExpression(RxParser::ConditionClosedBitXorExpressionContext *ctx) {
    if (ctx->CARET().size() == 0u) {
        return anyExpr(visit(ctx->conditionClosedBitAndExpression()));
    }
    auto operands = ctx->conditionBitAndExpression();
    auto ops = ctx->CARET();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedBitAndExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionClosedBitXorExpression
    : (conditionBitAndExpression CARET)* conditionClosedBitAndExpression
    ;
*/

std::any ASTVisitor::visitConditionBitAndExpression(RxParser::ConditionBitAndExpressionContext *ctx) {
    auto operands = ctx->conditionShiftExpression();
    auto ops = ctx->AMP();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBitAndExpression
    : conditionShiftExpression (AMP conditionShiftExpression)*
    ;
*/

std::any ASTVisitor::visitConditionClosedBitAndExpression(RxParser::ConditionClosedBitAndExpressionContext *ctx) {
    if (ctx->AMP().size() == 0u) {
        return anyExpr(visit(ctx->conditionClosedShiftExpression()));
    }
    auto operands = ctx->conditionShiftExpression();
    auto ops = ctx->AMP();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedShiftExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionClosedBitAndExpression
    : (conditionShiftExpression AMP)* conditionClosedShiftExpression
    ;
*/

std::any ASTVisitor::visitConditionShiftExpression(RxParser::ConditionShiftExpressionContext *ctx) {
    if (ctx->SHL().size() != 0u) {
        auto operands = ctx->conditionClosedAdditiveExpression();
        auto ops = ctx->SHL();
        Expr* left = anyExpr(visit(operands[0]));
        for (int i = 0; i < ops.size() - 1; i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i + 1]));
            left = now;
        }
        auto now = make<binaryExpr>();
        now->op_ = ops[ops.size() - 1]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(ctx->conditionAdditiveExpression(0)));
        return static_cast<Expr*>(now);
    }
    else {
        auto operands = ctx->conditionAdditiveExpression();
        auto ops = ctx->shiftRight();
        Expr* left = anyExpr(visit(operands[0]));
        for (int i = 0; i < ops.size(); i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i + 1]));
            left = now;
        }
        return static_cast<Expr*>(left);
    }
}
/*
conditionShiftExpression
    : (conditionClosedAdditiveExpression SHL | conditionAdditiveExpression shiftRight)* conditionAdditiveExpression
    ;
*/

std::any ASTVisitor::visitConditionClosedShiftExpression(RxParser::ConditionClosedShiftExpressionContext *ctx) {
    if (ctx->shiftRight().size() != 0u) {
        auto operands = ctx->conditionAdditiveExpression();
        auto ops = ctx->shiftRight();
        Expr* left = anyExpr(visit(operands[0]));
        for (int i = 0; i < ops.size() - 1; i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i + 1]));
            left = now;
        }
        auto now = make<binaryExpr>();
        now->op_ = ops[ops.size() - 1]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(ctx->conditionClosedAdditiveExpression(0)));
        return static_cast<Expr*>(now);
    }
    else {
        auto operands = ctx->conditionClosedAdditiveExpression();
        auto ops = ctx->SHL();
        Expr* left = anyExpr(visit(operands[0]));
        for (int i = 0; i < ops.size(); i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i + 1]));
            left = now;
        }
        return static_cast<Expr*>(left);
    }
}
/*
conditionClosedShiftExpression
    : (conditionClosedAdditiveExpression SHL | conditionAdditiveExpression shiftRight)* conditionClosedAdditiveExpression
    ;
*/

std::any ASTVisitor::visitConditionAdditiveExpression(RxParser::ConditionAdditiveExpressionContext *ctx) {
    auto operands = ctx->conditionMultiplicativeExpression();
    auto ops = ctx->additiveOperator();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionAdditiveExpression
    : conditionMultiplicativeExpression (additiveOperator conditionMultiplicativeExpression)*
    ;
*/

std::any ASTVisitor::visitConditionClosedAdditiveExpression(RxParser::ConditionClosedAdditiveExpressionContext *ctx) {
    if (ctx->additiveOperator().size() == 0u) {
        return anyExpr(visit(ctx->conditionClosedMultiplicativeExpression()));
    }
    auto operands = ctx->conditionMultiplicativeExpression();
    auto ops = ctx->additiveOperator();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedMultiplicativeExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionClosedAdditiveExpression
    : (conditionMultiplicativeExpression additiveOperator)* conditionClosedMultiplicativeExpression
    ;
*/

std::any ASTVisitor::visitConditionMultiplicativeExpression(RxParser::ConditionMultiplicativeExpressionContext *ctx) {
    auto operands = ctx->conditionCastExpression();
    auto ops = ctx->multiplicativeOperator();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionMultiplicativeExpression
    : conditionCastExpression (multiplicativeOperator conditionCastExpression)*
    ;
*/

std::any ASTVisitor::visitConditionClosedMultiplicativeExpression(RxParser::ConditionClosedMultiplicativeExpressionContext *ctx) {
    if (ctx->multiplicativeOperator().size() == 0u) {
        return anyExpr(visit(ctx->conditionClosedCastExpression()));
    }
    auto operands = ctx->conditionCastExpression();
    auto ops = ctx->multiplicativeOperator();
    Expr* left = anyExpr(visit(operands[0]));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i + 1]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedCastExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionClosedMultiplicativeExpression
    : (conditionCastExpression multiplicativeOperator)* conditionClosedCastExpression
    ;
*/

std::any ASTVisitor::visitConditionCastExpression(RxParser::ConditionCastExpressionContext *ctx) {
    Expr* now = anyExpr(visit(ctx->conditionUnaryExpression()));
    auto types = ctx->typeRef();
    for (auto cas : types) {
        auto cast = make<castExpr>();
        cast->from_ = now;
        cast->to_ = anyType(visit(cas));
        now = cast;
    }
    return static_cast<Expr*>(now);
}
/*
conditionCastExpression
    : conditionUnaryExpression (AS typeRef)*
    ;
*/

std::any ASTVisitor::visitConditionClosedCastExpression(RxParser::ConditionClosedCastExpressionContext *ctx) {
    if (auto un = ctx->conditionUnaryExpression(); un) { return anyExpr(visit(un)); }
    Expr* from = anyExpr(visit(ctx->conditionCastExpression()));
    auto now = make<castExpr>();
    now->from_ = from;
    now->to_ = anyType(visit(ctx->closedCastType()));
    return static_cast<Expr*>(now);
}
/*
conditionClosedCastExpression
    : conditionUnaryExpression
    | conditionCastExpression AS closedCastType
    ;
*/

std::any ASTVisitor::visitConditionUnaryExpression(RxParser::ConditionUnaryExpressionContext *ctx) {
    if (auto un = ctx->conditionUnaryExpression()) {
        std::string op = ctx->unaryOperator()->getText();
        Expr* operand = anyExpr(visit(un));
        if (op == "*") {
            auto now = make<derefExpr>();
            now->operand_ = operand;
            return static_cast<Expr*>(now);
        }
        else if (!op.empty() && op[0] == '&') {
            bool isMut = op.find("mut") != std::string::npos;
            int n = (op.size() > 1 && op[1] == '&') ? 2 : 1;
            auto refMake = [&](bool isMut, Expr* value) {
                auto ref = make<refExpr>();
                ref->isMut_ = isMut;
                ref->operand_ = value;
                return static_cast<Expr*>(ref);
            };
            if (n == 1) {
                return static_cast<Expr*>(refMake(isMut, operand));
            }
            else {
                return static_cast<Expr*>(refMake(false, static_cast<Expr*>(refMake(isMut, operand))));
            }
        }
        else {
            auto now = make<unaryExpr>();
            now->op_ = op;
            now->operand_ = operand;
            return static_cast<Expr*>(now);
        }
    }
    return anyExpr(visit(ctx->conditionPostfixExpression()));
}
/*
conditionUnaryExpression
    : unaryOperator conditionUnaryExpression
    | conditionPostfixExpression
    ;
*/

std::any ASTVisitor::visitConditionPostfixExpression(RxParser::ConditionPostfixExpressionContext *ctx) {
    Expr* now = anyExpr(visit(ctx->conditionPrimary()));
    return static_cast<Expr*>(appendSuffices(now, ctx->postfixSuffix()));
}
/*
conditionPostfixExpression
    : conditionPrimary postfixSuffix*
    ;
*/

std::any ASTVisitor::visitConditionBreakExpression(RxParser::ConditionBreakExpressionContext *ctx) {
    return anyExpr(visit(ctx->conditionBreakAssignmentExpression()));
}
/*
conditionBreakExpression
    : conditionBreakAssignmentExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakAssignmentExpression(RxParser::ConditionBreakAssignmentExpressionContext *ctx) {
    Expr* lhs = anyExpr(visit(ctx->conditionBreakLogicalOrExpression()));
    if (!ctx->assignmentOperator()) {
        return static_cast<Expr*>(lhs);
    }
    auto now = make<binaryExpr>();
    now->op_ = ctx->assignmentOperator()->getText();
    now->lhs_ = lhs;
    now->rhs_ = anyExpr(visit(ctx->conditionExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionBreakAssignmentExpression
    : conditionBreakLogicalOrExpression (assignmentOperator conditionExpression)?
    ;
*/

std::any ASTVisitor::visitConditionBreakLogicalOrExpression(RxParser::ConditionBreakLogicalOrExpressionContext *ctx) {
    auto operands = ctx->conditionLogicalAndExpression();
    auto ops = ctx->OROR();
    Expr* left = anyExpr(visit(ctx->conditionBreakLogicalAndExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBreakLogicalOrExpression
    : conditionBreakLogicalAndExpression (OROR conditionLogicalAndExpression)*
    ;

Note that here differs with former similar ones.
*/

std::any ASTVisitor::visitConditionBreakLogicalAndExpression(RxParser::ConditionBreakLogicalAndExpressionContext *ctx) {
    auto operands = ctx->conditionComparisonExpression();
    auto ops = ctx->ANDAND();
    Expr* left = anyExpr(visit(ctx->conditionBreakComparisonExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBreakLogicalAndExpression
    : conditionBreakComparisonExpression (ANDAND conditionComparisonExpression)*
    ;
*/

std::any ASTVisitor::visitConditionBreakComparisonExpression(RxParser::ConditionBreakComparisonExpressionContext *ctx) {
    if (ctx->LT()) {
        auto now = make<binaryExpr>();
        now->op_ = ctx->LT()->getText();
        now->lhs_ = anyExpr(visit(ctx->conditionBreakClosedBitOrExpression()));
        now->rhs_ = anyExpr(visit(ctx->conditionBitOrExpression()));
        return static_cast<Expr*>(now);
    }
    auto lhs = anyExpr(visit(ctx->conditionBreakBitOrExpression()));
    if (!ctx->comparisonExceptLt()) {
        return static_cast<Expr*>(lhs);
    }
    auto now = make<binaryExpr>();
    now->op_ = ctx->comparisonExceptLt()->getText();
    now->lhs_ = lhs;
    now->rhs_ = anyExpr(visit(ctx->conditionBitOrExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionBreakComparisonExpression
    : conditionBreakBitOrExpression (comparisonExceptLt conditionBitOrExpression)?
    | conditionBreakClosedBitOrExpression LT conditionBitOrExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakBitOrExpression(RxParser::ConditionBreakBitOrExpressionContext *ctx) {
    auto operands = ctx->conditionBitXorExpression();
    auto ops = ctx->PIPE();
    Expr* left = anyExpr(visit(ctx->conditionBreakBitXorExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBreakBitOrExpression
    : conditionBreakBitXorExpression (PIPE conditionBitXorExpression)*
    ;
*/

std::any ASTVisitor::visitConditionBreakClosedBitOrExpression(RxParser::ConditionBreakClosedBitOrExpressionContext *ctx) {
    if (ctx->PIPE().size() == 0u) {
        return anyExpr(visit(ctx->conditionBreakClosedBitXorExpression()));
    }
    auto operands = ctx->conditionBitXorExpression();
    auto ops = ctx->PIPE();
    Expr* left = anyExpr(visit(ctx->conditionBreakBitXorExpression()));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedBitXorExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionBreakClosedBitOrExpression
    : conditionBreakClosedBitXorExpression
    | conditionBreakBitXorExpression PIPE (conditionBitXorExpression PIPE)* conditionClosedBitXorExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakBitXorExpression(RxParser::ConditionBreakBitXorExpressionContext *ctx) {
    auto operands = ctx->conditionBitAndExpression();
    auto ops = ctx->CARET();
    Expr* left = anyExpr(visit(ctx->conditionBreakBitAndExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBreakBitXorExpression
    : conditionBreakBitAndExpression (CARET conditionBitAndExpression)*
    ;
*/

std::any ASTVisitor::visitConditionBreakClosedBitXorExpression(RxParser::ConditionBreakClosedBitXorExpressionContext *ctx) {
    if (ctx->CARET().size() == 0u) {
        return anyExpr(visit(ctx->conditionBreakClosedBitAndExpression()));
    }
    auto operands = ctx->conditionBitAndExpression();
    auto ops = ctx->CARET();
    Expr* left = anyExpr(visit(ctx->conditionBreakBitAndExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedBitAndExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionBreakClosedBitXorExpression
    : conditionBreakClosedBitAndExpression
    | conditionBreakBitAndExpression CARET (conditionBitAndExpression CARET)* conditionClosedBitAndExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakBitAndExpression(RxParser::ConditionBreakBitAndExpressionContext *ctx) {
    auto operands = ctx->conditionShiftExpression();
    auto ops = ctx->AMP();
    Expr* left = anyExpr(visit(ctx->conditionBreakShiftExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBreakBitAndExpression
    : conditionBreakShiftExpression (AMP conditionShiftExpression)*
    ;
*/

std::any ASTVisitor::visitConditionBreakClosedBitAndExpression(RxParser::ConditionBreakClosedBitAndExpressionContext *ctx) {
    if (ctx->AMP().size() == 0u) {
        return anyExpr(visit(ctx->conditionBreakClosedShiftExpression()));
    }
    auto operands = ctx->conditionShiftExpression();
    auto ops = ctx->AMP();
    Expr* left = anyExpr(visit(ctx->conditionBreakShiftExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedShiftExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionBreakClosedBitAndExpression
    : conditionBreakClosedShiftExpression
    | conditionBreakShiftExpression AMP (conditionShiftExpression AMP)* conditionClosedShiftExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakShiftExpression(RxParser::ConditionBreakShiftExpressionContext *ctx) {
    if (ctx->conditionAdditiveExpression().size() == 0u) {
        return anyExpr(visit(ctx->conditionBreakAdditiveExpression()));
    }
    if (ctx->SHL().size() != 0u) {
        auto operands = ctx->conditionClosedAdditiveExpression();
        auto ops = ctx->SHL();
        Expr* left = anyExpr(visit(ctx->conditionBreakClosedAdditiveExpression()));
        for (int i = 0; i < ops.size() - 1; i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i]));
            left = now;
        }
        auto now = make<binaryExpr>();
        now->op_ = ops[ops.size() - 1]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(ctx->conditionAdditiveExpression(0)));
        return static_cast<Expr*>(now);
    }
    else {
        auto operands = ctx->conditionAdditiveExpression();
        auto ops = ctx->shiftRight();
        Expr* left = anyExpr(visit(ctx->conditionBreakAdditiveExpression()));
        for (int i = 0; i < ops.size(); i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i]));
            left = now;
        }
        return static_cast<Expr*>(left);
    }
}
/*
conditionBreakShiftExpression
    : conditionBreakAdditiveExpression
    | (conditionBreakClosedAdditiveExpression SHL | conditionBreakAdditiveExpression shiftRight)
      (conditionClosedAdditiveExpression SHL | conditionAdditiveExpression shiftRight)* conditionAdditiveExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakClosedShiftExpression(RxParser::ConditionBreakClosedShiftExpressionContext *ctx) {
    if (ctx->conditionClosedAdditiveExpression().size() == 0u) {
        return anyExpr(visit(ctx->conditionBreakAdditiveExpression()));
    }
    if (ctx->shiftRight().size() != 0u) {
        auto operands = ctx->conditionAdditiveExpression();
        auto ops = ctx->shiftRight();
        Expr* left = anyExpr(visit(ctx->conditionBreakAdditiveExpression()));
        for (int i = 0; i < ops.size() - 1; i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i]));
            left = now;
        }
        auto now = make<binaryExpr>();
        now->op_ = ops[ops.size() - 1]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(ctx->conditionClosedAdditiveExpression(0)));
        return static_cast<Expr*>(now);
    }
    else {
        auto operands = ctx->conditionClosedAdditiveExpression();
        auto ops = ctx->SHL();
        Expr* left = anyExpr(visit(ctx->conditionBreakClosedAdditiveExpression()));
        for (int i = 0; i < ops.size(); i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i]));
            left = now;
        }
        return static_cast<Expr*>(left);
    }
}
/*
conditionBreakClosedShiftExpression
    : conditionBreakClosedAdditiveExpression
    | (conditionBreakClosedAdditiveExpression SHL | conditionBreakAdditiveExpression shiftRight)
      (conditionClosedAdditiveExpression SHL | conditionAdditiveExpression shiftRight)* conditionClosedAdditiveExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakAdditiveExpression(RxParser::ConditionBreakAdditiveExpressionContext *ctx) {
    auto operands = ctx->conditionMultiplicativeExpression();
    auto ops = ctx->additiveOperator();
    Expr* left = anyExpr(visit(ctx->conditionBreakMultiplicativeExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBreakAdditiveExpression
    : conditionBreakMultiplicativeExpression (additiveOperator conditionMultiplicativeExpression)*
    ;
*/

std::any ASTVisitor::visitConditionBreakClosedAdditiveExpression(RxParser::ConditionBreakClosedAdditiveExpressionContext *ctx) {
    if (ctx->additiveOperator().size() == 0u) {
        return anyExpr(visit(ctx->conditionBreakClosedMultiplicativeExpression()));
    }
    auto operands = ctx->conditionMultiplicativeExpression();
    auto ops = ctx->additiveOperator();
    Expr* left = anyExpr(visit(ctx->conditionBreakMultiplicativeExpression()));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedMultiplicativeExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionBreakClosedAdditiveExpression
    : conditionBreakClosedMultiplicativeExpression
    | conditionBreakMultiplicativeExpression additiveOperator
      (conditionMultiplicativeExpression additiveOperator)* conditionClosedMultiplicativeExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakMultiplicativeExpression(RxParser::ConditionBreakMultiplicativeExpressionContext *ctx) {
    auto operands = ctx->conditionCastExpression();
    auto ops = ctx->multiplicativeOperator();
    Expr* left = anyExpr(visit(ctx->conditionBreakCastExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
conditionBreakMultiplicativeExpression
    : conditionBreakCastExpression (multiplicativeOperator conditionCastExpression)*
    ;
*/

std::any ASTVisitor::visitConditionBreakClosedMultiplicativeExpression(RxParser::ConditionBreakClosedMultiplicativeExpressionContext *ctx) {
    if (ctx->multiplicativeOperator().size() == 0u) {
        return anyExpr(visit(ctx->conditionBreakClosedCastExpression()));
    }
    auto operands = ctx->conditionCastExpression();
    auto ops = ctx->multiplicativeOperator();
    Expr* left = anyExpr(visit(ctx->conditionBreakCastExpression()));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->conditionClosedCastExpression()));
    return static_cast<Expr*>(now);
}
/*
conditionBreakClosedMultiplicativeExpression
    : conditionBreakClosedCastExpression
    | conditionBreakCastExpression multiplicativeOperator
      (conditionCastExpression multiplicativeOperator)* conditionClosedCastExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakCastExpression(RxParser::ConditionBreakCastExpressionContext *ctx) {
    Expr* now = anyExpr(visit(ctx->conditionBreakUnaryExpression()));
    auto types = ctx->typeRef();
    for (auto cas : types) {
        auto cast = make<castExpr>();
        cast->from_ = now;
        cast->to_ = anyType(visit(cas));
        now = cast;
    }
    return static_cast<Expr*>(now);
}
/*
conditionBreakCastExpression
    : conditionBreakUnaryExpression (AS typeRef)*
    ;
*/

std::any ASTVisitor::visitConditionBreakClosedCastExpression(RxParser::ConditionBreakClosedCastExpressionContext *ctx) {
    if (auto un = ctx->conditionBreakUnaryExpression(); un) { return anyExpr(visit(un)); }
    Expr* from = anyExpr(visit(ctx->conditionBreakCastExpression()));
    auto now = make<castExpr>();
    now->from_ = from;
    now->to_ = anyType(visit(ctx->closedCastType()));
    return static_cast<Expr*>(now);
}
/*
conditionBreakClosedCastExpression
    : conditionBreakUnaryExpression
    | conditionBreakCastExpression AS closedCastType
    ;
*/

std::any ASTVisitor::visitConditionBreakUnaryExpression(RxParser::ConditionBreakUnaryExpressionContext *ctx) {
    if (auto un = ctx->conditionUnaryExpression()) {
        std::string op = ctx->unaryOperator()->getText();
        Expr* operand = anyExpr(visit(un));
        if (op == "*") {
            auto now = make<derefExpr>();
            now->operand_ = operand;
            return static_cast<Expr*>(now);
        }
        else if (!op.empty() && op[0] == '&') {
            bool isMut = op.find("mut") != std::string::npos;
            int n = (op.size() > 1 && op[1] == '&') ? 2 : 1;
            auto refMake = [&](bool isMut, Expr* value) {
                auto ref = make<refExpr>();
                ref->isMut_ = isMut;
                ref->operand_ = value;
                return static_cast<Expr*>(ref);
            };
            if (n == 1) {
                return static_cast<Expr*>(refMake(isMut, operand));
            }
            else {
                return static_cast<Expr*>(refMake(false, static_cast<Expr*>(refMake(isMut, operand))));
            }
        }
        else {
            auto now = make<unaryExpr>();
            now->op_ = op;
            now->operand_ = operand;
            return static_cast<Expr*>(now);
        }
    }
    return anyExpr(visit(ctx->conditionBreakPostfixExpression()));
}
/*
conditionBreakUnaryExpression
    : unaryOperator conditionUnaryExpression
    | conditionBreakPostfixExpression
    ;
*/

std::any ASTVisitor::visitConditionBreakPostfixExpression(RxParser::ConditionBreakPostfixExpressionContext *ctx) {
    Expr* now = anyExpr(visit(ctx->conditionPrimaryWithoutBareBlock()));
    return static_cast<Expr*>(appendSuffices(now, ctx->postfixSuffix()));
}
/*
conditionBreakPostfixExpression
    : conditionPrimaryWithoutBareBlock postfixSuffix*
    ;
*/
// 10.4 night ended here
// This is too much work even just copy-pasting the similar but different block.
// How can prior guys finish AST and semantic check in just four weeks without AI?
// I don't think that everyone before had sacrifice their National's Day holiday totally on this.

std::any ASTVisitor::visitStatementExpression(RxParser::StatementExpressionContext *ctx) {
    return anyExpr(visit(ctx->statementAssignmentExpression()));
}
/*
statementExpression
    : statementAssignmentExpression
    ;
*/

std::any ASTVisitor::visitStatementAssignmentExpression(RxParser::StatementAssignmentExpressionContext *ctx) {
    Expr* lhs = anyExpr(visit(ctx->statementLogicalOrExpression()));
    if (!ctx->assignmentOperator()) {
        return static_cast<Expr*>(lhs);
    }
    auto now = make<binaryExpr>();
    now->op_ = ctx->assignmentOperator()->getText();
    now->lhs_ = lhs;
    now->rhs_ = anyExpr(visit(ctx->expression()));
    return static_cast<Expr*>(now);
}
/*
statementAssignmentExpression
    : statementLogicalOrExpression (assignmentOperator expression)?
    ;
*/

std::any ASTVisitor::visitStatementLogicalOrExpression(RxParser::StatementLogicalOrExpressionContext *ctx) {
    auto operands = ctx->logicalAndExpression();
    auto ops = ctx->OROR();
    Expr* left = anyExpr(visit(ctx->statementLogicalAndExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
statementLogicalOrExpression
    : statementLogicalAndExpression (OROR logicalAndExpression)*
    ;
*/

std::any ASTVisitor::visitStatementLogicalAndExpression(RxParser::StatementLogicalAndExpressionContext *ctx) {
    auto operands = ctx->comparisonExpression();
    auto ops = ctx->ANDAND();
    Expr* left = anyExpr(visit(ctx->statementComparisonExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
statementLogicalAndExpression
    : statementComparisonExpression (ANDAND comparisonExpression)*
    ;
*/

std::any ASTVisitor::visitStatementComparisonExpression(RxParser::StatementComparisonExpressionContext *ctx) {
    if (ctx->LT()) {
        auto now = make<binaryExpr>();
        now->op_ = ctx->LT()->getText();
        now->lhs_ = anyExpr(visit(ctx->statementClosedBitOrExpression()));
        now->rhs_ = anyExpr(visit(ctx->bitOrExpression()));
        return static_cast<Expr*>(now);
    }
    auto lhs = anyExpr(visit(ctx->statementBitOrExpression()));
    if (!ctx->comparisonExceptLt()) {
        return static_cast<Expr*>(lhs);
    }
    auto now = make<binaryExpr>();
    now->op_ = ctx->comparisonExceptLt()->getText();
    now->lhs_ = lhs;
    now->rhs_ = anyExpr(visit(ctx->bitOrExpression()));
    return static_cast<Expr*>(now);
}
/*
statementComparisonExpression
    : statementBitOrExpression (comparisonExceptLt bitOrExpression)?
    | statementClosedBitOrExpression LT bitOrExpression
    ;
*/

std::any ASTVisitor::visitStatementBitOrExpression(RxParser::StatementBitOrExpressionContext *ctx) {
    auto operands = ctx->bitXorExpression();
    auto ops = ctx->PIPE();
    Expr* left = anyExpr(visit(ctx->statementBitXorExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
statementBitOrExpression
    : statementBitXorExpression (PIPE bitXorExpression)*
    ;
*/

std::any ASTVisitor::visitStatementClosedBitOrExpression(RxParser::StatementClosedBitOrExpressionContext *ctx) {
    if (ctx->PIPE().size() == 0u) {
        return anyExpr(visit(ctx->statementClosedBitXorExpression()));
    }
    auto operands = ctx->bitXorExpression();
    auto ops = ctx->PIPE();
    Expr* left = anyExpr(visit(ctx->statementBitXorExpression()));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedBitXorExpression()));
    return static_cast<Expr*>(now);
}
/*
statementClosedBitOrExpression
    : statementClosedBitXorExpression
    | statementBitXorExpression PIPE (bitXorExpression PIPE)* closedBitXorExpression
    ;
*/

std::any ASTVisitor::visitStatementBitXorExpression(RxParser::StatementBitXorExpressionContext *ctx) {
    auto operands = ctx->bitAndExpression();
    auto ops = ctx->CARET();
    Expr* left = anyExpr(visit(ctx->statementBitAndExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
statementBitXorExpression
    : statementBitAndExpression (CARET bitAndExpression)*
    ;
*/

std::any ASTVisitor::visitStatementClosedBitXorExpression(RxParser::StatementClosedBitXorExpressionContext *ctx) {
    if (ctx->CARET().size() == 0u) {
        return anyExpr(visit(ctx->statementClosedBitAndExpression()));
    }
    auto operands = ctx->bitAndExpression();
    auto ops = ctx->CARET();
    Expr* left = anyExpr(visit(ctx->statementBitAndExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedBitAndExpression()));
    return static_cast<Expr*>(now);
}
/*
statementClosedBitXorExpression
    : statementClosedBitAndExpression
    | statementBitAndExpression CARET (bitAndExpression CARET)* closedBitAndExpression
    ;
*/

std::any ASTVisitor::visitStatementBitAndExpression(RxParser::StatementBitAndExpressionContext *ctx) {
    auto operands = ctx->shiftExpression();
    auto ops = ctx->AMP();
    Expr* left = anyExpr(visit(ctx->statementShiftExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
statementBitAndExpression
    : statementShiftExpression (AMP shiftExpression)*
    ;
*/

std::any ASTVisitor::visitStatementClosedBitAndExpression(RxParser::StatementClosedBitAndExpressionContext *ctx) {
    if (ctx->AMP().size() == 0u) {
        return anyExpr(visit(ctx->statementClosedShiftExpression()));
    }
    auto operands = ctx->shiftExpression();
    auto ops = ctx->AMP();
    Expr* left = anyExpr(visit(ctx->statementShiftExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedShiftExpression()));
    return static_cast<Expr*>(now);
}
/*
statementClosedBitAndExpression
    : statementClosedShiftExpression
    | statementShiftExpression AMP (shiftExpression AMP)* closedShiftExpression
    ;
*/

std::any ASTVisitor::visitStatementShiftExpression(RxParser::StatementShiftExpressionContext *ctx) {
    if (ctx->additiveExpression().size() == 0u) {
        return anyExpr(visit(ctx->statementAdditiveExpression()));
    }
    if (ctx->SHL().size() != 0u) {
        auto operands = ctx->closedAdditiveExpression();
        auto ops = ctx->SHL();
        Expr* left = anyExpr(visit(ctx->statementClosedAdditiveExpression()));
        for (int i = 0; i < ops.size() - 1; i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i]));
            left = now;
        }
        auto now = make<binaryExpr>();
        now->op_ = ops[ops.size() - 1]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(ctx->additiveExpression(0)));
        return static_cast<Expr*>(now);
    }
    else {
        auto operands = ctx->additiveExpression();
        auto ops = ctx->shiftRight();
        Expr* left = anyExpr(visit(ctx->statementAdditiveExpression()));
        for (int i = 0; i < ops.size(); i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i]));
            left = now;
        }
        return static_cast<Expr*>(left);
    }
}
/*
statementShiftExpression
    : statementAdditiveExpression
    | (statementClosedAdditiveExpression SHL | statementAdditiveExpression shiftRight)
      (closedAdditiveExpression SHL | additiveExpression shiftRight)* additiveExpression
    ;
*/

std::any ASTVisitor::visitStatementClosedShiftExpression(RxParser::StatementClosedShiftExpressionContext *ctx) {
    if (ctx->closedAdditiveExpression().size() == 0u) {
        return anyExpr(visit(ctx->statementClosedAdditiveExpression()));
    }
    if (ctx->shiftRight().size() != 0u) {
        auto operands = ctx->additiveExpression();
        auto ops = ctx->shiftRight();
        Expr* left = anyExpr(visit(ctx->statementAdditiveExpression()));
        for (int i = 0; i < ops.size() - 1; i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i]));
            left = now;
        }
        auto now = make<binaryExpr>();
        now->op_ = ops[ops.size() - 1]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(ctx->closedAdditiveExpression(0)));
        return static_cast<Expr*>(now);
    }
    else {
        auto operands = ctx->closedAdditiveExpression();
        auto ops = ctx->SHL();
        Expr* left = anyExpr(visit(ctx->statementClosedAdditiveExpression()));
        for (int i = 0; i < ops.size(); i++) {
            auto now = make<binaryExpr>();
            now->op_ = ops[i]->getText();
            now->lhs_ = left;
            now->rhs_ = anyExpr(visit(operands[i]));
            left = now;
        }
        return static_cast<Expr*>(left);
    }
}
/*
statementClosedShiftExpression
    : statementClosedAdditiveExpression
    | (statementClosedAdditiveExpression SHL | statementAdditiveExpression shiftRight)
      (closedAdditiveExpression SHL | additiveExpression shiftRight)* closedAdditiveExpression
    ;
*/

std::any ASTVisitor::visitStatementAdditiveExpression(RxParser::StatementAdditiveExpressionContext *ctx) {
    auto operands = ctx->multiplicativeExpression();
    auto ops = ctx->additiveOperator();
    Expr* left = anyExpr(visit(ctx->statementMultiplicativeExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
statementAdditiveExpression
    : statementMultiplicativeExpression (additiveOperator multiplicativeExpression)*
    ;
*/

std::any ASTVisitor::visitStatementClosedAdditiveExpression(RxParser::StatementClosedAdditiveExpressionContext *ctx) {
    if (ctx->additiveOperator().size() == 0u) {
        return anyExpr(visit(ctx->statementClosedMultiplicativeExpression()));
    }
    auto operands = ctx->multiplicativeExpression();
    auto ops = ctx->additiveOperator();
    Expr* left = anyExpr(visit(ctx->statementMultiplicativeExpression()));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedMultiplicativeExpression()));
    return static_cast<Expr*>(now);
}
/*
statementClosedAdditiveExpression
    : statementClosedMultiplicativeExpression
    | statementMultiplicativeExpression additiveOperator
      (multiplicativeExpression additiveOperator)* closedMultiplicativeExpression
    ;
*/

std::any ASTVisitor::visitStatementMultiplicativeExpression(RxParser::StatementMultiplicativeExpressionContext *ctx) {
    auto operands = ctx->castExpression();
    auto ops = ctx->multiplicativeOperator();
    Expr* left = anyExpr(visit(ctx->statementCastExpression()));
    for (int i = 0; i < ops.size(); i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    return static_cast<Expr*>(left);
}
/*
statementMultiplicativeExpression
    : statementCastExpression (multiplicativeOperator castExpression)*
    ;
*/

std::any ASTVisitor::visitStatementClosedMultiplicativeExpression(RxParser::StatementClosedMultiplicativeExpressionContext *ctx) {
    if (ctx->multiplicativeOperator().size() == 0u) {
        return anyExpr(visit(ctx->statementClosedCastExpression()));
    }
    auto operands = ctx->castExpression();
    auto ops = ctx->multiplicativeOperator();
    Expr* left = anyExpr(visit(ctx->statementCastExpression()));
    for (int i = 0; i < ops.size() - 1; i++) {
        auto now = make<binaryExpr>();
        now->op_ = ops[i]->getText();
        now->lhs_ = left;
        now->rhs_ = anyExpr(visit(operands[i]));
        left = now;
    }
    auto now = make<binaryExpr>();
    now->op_ = ops[ops.size() - 1]->getText();
    now->lhs_ = left;
    now->rhs_ = anyExpr(visit(ctx->closedCastExpression()));
    return static_cast<Expr*>(now);
}
/*
statementClosedMultiplicativeExpression
    : statementClosedCastExpression
    | statementCastExpression multiplicativeOperator
      (castExpression multiplicativeOperator)* closedCastExpression
    ;
*/

std::any ASTVisitor::visitStatementCastExpression(RxParser::StatementCastExpressionContext *ctx) {
    Expr* now = anyExpr(visit(ctx->statementUnaryExpression()));
    auto types = ctx->typeRef();
    for (auto cas : types) {
        auto cast = make<castExpr>();
        cast->from_ = now;
        cast->to_ = anyType(visit(cas));
        now = cast;
    }
    return static_cast<Expr*>(now);
}
/*
statementCastExpression
    : statementUnaryExpression (AS typeRef)*
    ;
*/

std::any ASTVisitor::visitStatementClosedCastExpression(RxParser::StatementClosedCastExpressionContext *ctx) {
    if (auto un = ctx->statementUnaryExpression(); un) { return anyExpr(visit(un)); }
    Expr* from = anyExpr(visit(ctx->statementCastExpression()));
    auto now = make<castExpr>();
    now->from_ = from;
    now->to_ = anyType(visit(ctx->closedCastType()));
    return static_cast<Expr*>(now);
}
/*
statementClosedCastExpression
    : statementUnaryExpression
    | statementCastExpression AS closedCastType
    ;
*/

std::any ASTVisitor::visitStatementUnaryExpression(RxParser::StatementUnaryExpressionContext *ctx) {
    if (auto un = ctx->unaryExpression()) {
        std::string op = ctx->unaryOperator()->getText();
        Expr* operand = anyExpr(visit(un));
        if (op == "*") {
            auto now = make<derefExpr>();
            now->operand_ = operand;
            return static_cast<Expr*>(now);
        }
        else if (!op.empty() && op[0] == '&') {
            bool isMut = op.find("mut") != std::string::npos;
            int n = (op.size() > 1 && op[1] == '&') ? 2 : 1;
            auto refMake = [&](bool isMut, Expr* value) {
                auto ref = make<refExpr>();
                ref->isMut_ = isMut;
                ref->operand_ = value;
                return static_cast<Expr*>(ref);
            };
            if (n == 1) {
                return static_cast<Expr*>(refMake(isMut, operand));
            }
            else {
                return static_cast<Expr*>(refMake(false, static_cast<Expr*>(refMake(isMut, operand))));
            }
        }
        else {
            auto now = make<unaryExpr>();
            now->op_ = op;
            now->operand_ = operand;
            return static_cast<Expr*>(now);
        }
    }
    return anyExpr(visit(ctx->statementPostfixExpression()));
}
/*
statementUnaryExpression
    : unaryOperator unaryExpression
    | statementPostfixExpression
    ;
*/

std::any ASTVisitor::visitStatementPostfixExpression(RxParser::StatementPostfixExpressionContext *ctx) {
    if (ctx->nonBlockPrimary()) {
        Expr* now = anyExpr(visit(ctx->nonBlockPrimary()));
        return static_cast<Expr*>(appendSuffices(now, ctx->postfixSuffix()));
    }
    else {
        Expr* now = anyExpr(visit(ctx->expressionWithBlock()));
        return static_cast<Expr*>(appendSuffices(appendDotSuffix(now, ctx->dotSuffix()), ctx->postfixSuffix()));
    }
}
/*
statementPostfixExpression
    : nonBlockPrimary postfixSuffix*
    | expressionWithBlock dotSuffix postfixSuffix*
    ;
*/

std::any ASTVisitor::visitPrimaryExpression(RxParser::PrimaryExpressionContext *ctx) {
    if (ctx->nonBlockPrimary()) {
        return anyExpr(visit(ctx->nonBlockPrimary()));
    }
    else {
        return anyExpr(visit(ctx->expressionWithBlock()));
    }
    return visitChildren(ctx);
}
/*
primaryExpression
    : nonBlockPrimary
    | expressionWithBlock
    ;
*/

std::any ASTVisitor::visitNonBlockPrimary(RxParser::NonBlockPrimaryContext *ctx) {
    if (auto lit = ctx->literalExpression(); lit) {
        return anyExpr(visit(lit));
    }
    if (auto path = ctx->pathInExpression(); path) {
        if (ctx->LBRACE()) {
            auto now = make<structInitExpr>();
            now->path_ = anyExpr(visit(path));
            if (auto fields = ctx->structExprFields(); fields) {
                auto field = fields->structExprField();
                for (auto cur : field) {
                    fieldInit f;
                    f.name_ = cur->identifier()->getText();
                    f.value_ = anyExpr(visit(cur->expression()));
                    now->fields_.push_back(f);
                }
            }
            return static_cast<Expr*>(now);
        }
        return anyExpr(visit(path));
    }
    if (ctx->LPAREN()) {
        if (auto exp = ctx->expression(); exp) {
            return anyExpr(visit(exp));
        }
        auto now = make<unitExpr>();
        return static_cast<Expr*>(now);
    }
    if (auto arr = ctx->arrayExpression(); arr) {
        return anyExpr(visit(arr));
    }
    if (ctx->BREAK()) {
        auto now = make<breakExpr>();
        if (ctx->expression()) {
            now->tail_ = anyExpr(visit(ctx->expression()));
        }
        return static_cast<Expr*>(now);
    }
    if (ctx->RETURN()) {
        auto now = make<returnExpr>();
        if (ctx->expression()) {
            now->tail_ = anyExpr(visit(ctx->expression()));
        }
        return static_cast<Expr*>(now);
    }
    if (ctx->CONTINUE()) {
        auto now = make<continueExpr>();
        return static_cast<Expr*>(now);
    }
    return visitChildren(ctx);
}
/*
nonBlockPrimary
    : literalExpression
    | pathInExpression (LBRACE structExprFields? RBRACE)?
    | LPAREN expression? RPAREN
    | arrayExpression
    | BREAK expression?
    | RETURN expression?
    | CONTINUE
    ;
*/

std::any ASTVisitor::visitConditionPrimary(RxParser::ConditionPrimaryContext *ctx) {
    if (ctx->conditionPrimaryWithoutBareBlock()) {
        return anyExpr(visit(ctx->conditionPrimaryWithoutBareBlock()));
    }
    else {
        return anyExpr(visit(ctx->blockExpression()));
    }
}
/*
conditionPrimary
    : conditionPrimaryWithoutBareBlock
    | blockExpression
    ;
*/

std::any ASTVisitor::visitConditionPrimaryWithoutBareBlock(RxParser::ConditionPrimaryWithoutBareBlockContext *ctx) {
    if (auto lit = ctx->literalExpression(); lit) {
        return anyExpr(visit(lit));
    }
    if (auto path = ctx->pathInExpression(); path) {
        return anyExpr(visit(path));
    }
    if (ctx->LPAREN()) {
        if (auto exp = ctx->expression(); exp) {
            return anyExpr(visit(exp));
        }
        auto now = make<unitExpr>();
        return static_cast<Expr*>(now);
    }
    if (auto arr = ctx->arrayExpression(); arr) {
        return anyExpr(visit(arr));
    }
    if (auto f = ctx->ifExpression(); f) {
        return anyExpr(visit(f));
    }
    if (ctx->LOOP()) {
        auto now = make<loopExpr>();
        now->block_ = static_cast<blockExpr*>(anyExpr(visit(ctx->blockExpression())));
        return static_cast<Expr*>(now);
    }
    if (ctx->WHILE()) {
        auto now = make<whileExpr>();
        now->cond_ = anyExpr(visit(ctx->conditionExpression()));
        now->block_ = static_cast<blockExpr*>(anyExpr(visit(ctx->blockExpression())));
        return static_cast<Expr*>(now);
    }
    if (ctx->BREAK()) {
        auto now = make<breakExpr>();
        if (ctx->expression()) {
            now->tail_ = anyExpr(visit(ctx->expression()));
        }
        return static_cast<Expr*>(now);
    }
    if (ctx->RETURN()) {
        auto now = make<returnExpr>();
        if (ctx->expression()) {
            now->tail_ = anyExpr(visit(ctx->expression()));
        }
        return static_cast<Expr*>(now);
    }
    if (ctx->CONTINUE()) {
        auto now = make<continueExpr>();
        return static_cast<Expr*>(now);
    }
    return visitChildren(ctx);
}
/*
conditionPrimaryWithoutBareBlock
    : literalExpression
    | pathInExpression
    | LPAREN expression? RPAREN
    | arrayExpression
    | ifExpression
    | LOOP blockExpression
    | WHILE conditionExpression blockExpression
    | BREAK conditionBreakExpression?
    | RETURN conditionExpression?
    | CONTINUE
    ;
*/

std::any ASTVisitor::visitLiteralExpression(RxParser::LiteralExpressionContext *ctx) {
    if (auto lit = ctx->INTEGER_LITERAL(); lit) {
        auto now = make<intLitExpr>();
        now->value_ = ctx->INTEGER_LITERAL()->getText();
        return static_cast<Expr*>(now);
    }
    auto now = make<boolLitExpr>();
    now->value_ = ctx->TRUE()? true : false;
    return static_cast<Expr*>(now);
}
/*
literalExpression
    : INTEGER_LITERAL | TRUE | FALSE
    ;
*/

std::any ASTVisitor::visitStructExprFields(RxParser::StructExprFieldsContext *ctx) {
    // Compressed
    throw "visitStructExprFields";
    return visitChildren(ctx);
}
/*
structExprFields
    : structExprField (COMMA structExprField)* COMMA?
    ;
*/

std::any ASTVisitor::visitStructExprField(RxParser::StructExprFieldContext *ctx) {
    // Compressed
    throw "visitStructExprField";
    return visitChildren(ctx);
}
/*
structExprField
    : identifier COLON expression
    ;
*/

std::any ASTVisitor::visitArrayExpression(RxParser::ArrayExpressionContext *ctx) {
    if (ctx->SEMI()) {
        auto now = make<repeatedArrayExpr>();
        now->value_ = anyExpr(visit(ctx->expression(0)));
        now->num_ = anyConstValue(visit(ctx->constValue()));
        return static_cast<Expr*>(now);
    }
    auto now = make<arrayExpr>();
    auto exprs = ctx->expression();
    for (auto expr : exprs) {
        now->args_.push_back(anyExpr(visit(expr)));
    }
    return static_cast<Expr*>(now);
}
/*
arrayExpression
    : LBRACKET (expression (SEMI constValue | (COMMA expression)* COMMA?))? RBRACKET
    ;
*/

std::any ASTVisitor::visitPostfixSuffix(RxParser::PostfixSuffixContext *ctx) {
    // Compressed
    throw "visitPostfixSuffix";
    return visitChildren(ctx);
}
/*
postfixSuffix
    : callArguments
    | LBRACKET expression RBRACKET
    | dotSuffix
    ;
*/

std::any ASTVisitor::visitDotSuffix(RxParser::DotSuffixContext *ctx) {
    // Compressed
    throw "visitDotSuffix";
    return visitChildren(ctx);
}
/*
dotSuffix
    : DOT pathExprSegment callArguments
    | DOT identifier
    ;
*/

std::any ASTVisitor::visitCallArguments(RxParser::CallArgumentsContext *ctx) {
    // Compressed
    throw "visitCallArguments";
    return visitChildren(ctx);
}
/*
callArguments
    : LPAREN (expression (COMMA expression)* COMMA?)? RPAREN
    ;
*/

std::any ASTVisitor::visitUnaryOperator(RxParser::UnaryOperatorContext *ctx) {
    // Compressed
    throw "visitUnaryOperator";
    return visitChildren(ctx);
}
/*
unaryOperator
    : MINUS | NOT | STAR | (AMP | ANDAND) MUT?
    ;
*/

std::any ASTVisitor::visitMultiplicativeOperator(RxParser::MultiplicativeOperatorContext *ctx) {
    // Compressed
    throw "visitMultiplicativeOperator";
    return visitChildren(ctx);
}
/*
multiplicativeOperator
    : STAR | SLASH | PERCENT
    ;
*/

std::any ASTVisitor::visitAdditiveOperator(RxParser::AdditiveOperatorContext *ctx) {
    // Compressed
    throw "visitAdditiveOperator";
    return visitChildren(ctx);
}
/*
additiveOperator
    : PLUS | MINUS
    ;
*/

std::any ASTVisitor::visitShiftRight(RxParser::ShiftRightContext *ctx) {
    // Compressed
    throw "visitShiftRight";
    return visitChildren(ctx);
}
/*
shiftRight
    : GT GT_SECOND
    ;
*/

std::any ASTVisitor::visitComparisonExceptLt(RxParser::ComparisonExceptLtContext *ctx) {
    // Compressed
    throw "visitComparisonExceptLt";
    return visitChildren(ctx);
}
/*
comparisonExceptLt
    : EQEQ | NE | LE | GT GE_EQ | GT_SECOND SHR_EQ | genericClose
    ;
*/

std::any ASTVisitor::visitAssignmentOperator(RxParser::AssignmentOperatorContext *ctx) {
    // Compressed
    throw "visitAssignmentOperator";
    return visitChildren(ctx);
}
/*
assignmentOperator
    : equalsSign | PLUS_ASSIGN | MINUS_ASSIGN | STAR_ASSIGN | SLASH_ASSIGN
    | PERCENT_ASSIGN | AMP_ASSIGN | PIPE_ASSIGN | CARET_ASSIGN
    | SHL_ASSIGN | GT GT_SECOND SHR_EQ
    ;
*/

std::any ASTVisitor::visitEqualsSign(RxParser::EqualsSignContext *ctx) {
    // Compressed
    throw "visitEqualsSign";
    return visitChildren(ctx);
}
/*
equalsSign
    : ASSIGN | GE_EQ | SHR_EQ
    ;
*/

std::any ASTVisitor::visitIdentifier(RxParser::IdentifierContext *ctx) {
    // Compressed
    throw "visitIdentifier";
    return visitChildren(ctx);
}
/*
identifier
    : IDENTIFIER | DERIVE | COPY | CLONE | PARTIAL_EQ | EQ
    ;
*/