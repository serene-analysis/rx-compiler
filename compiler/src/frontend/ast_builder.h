#pragma once
#include "ast/ast.h"
#include "RxParserBaseVisitor.h"
#include <memory>
#include <vector>

class ASTVisitor : public RxParserBaseVisitor {
private:
  std::vector<std::unique_ptr<Node>> pool_;

  template<typename Tp> Tp* make(){
    auto node = std::make_unique<Tp>();
    Tp* ret = node.get();
    pool_.push_back(std::move(node));
    return ret;
  }

  inline Type* makeNamedType(const std::string &name, const std::vector<Type*> &types) {
    auto final_type = make<Type>();
    if ((name == "Box" || name == "Vec") && types.size() == 1) {
      final_type->kind_ = (name == "Box" ? Type::kTypeKind::BOX : Type::kTypeKind::VEC);
      final_type->elem_ = types[0];
    }
    else if (name == "i32" || name == "u32" || name == "isize" || name == "usize" || name == "bool") {
      final_type->kind_ = Type::kTypeKind::BUILTIN;
      final_type->name_ = name;
    }
    else {
      final_type->kind_ = Type::kTypeKind::STRUCT;
      final_type->name_ = name;
    }
    return static_cast<Type*>(final_type);
  }

  Expr* appendDotSuffix(Expr* from, RxParser::DotSuffixContext* dot) {
    if (auto args = dot->callArguments(); args) {
      auto mcal = make<methodCallExpr>();
      mcal->from_ = from;
      mcal->name_ = dot->pathExprSegment()->pathIdentSegment()->getText();
      auto exprs = args->expression();
      for (auto expr : exprs) {
        mcal->args_.push_back(anyExpr(visit(expr)));
      }
      return static_cast<Expr*>(mcal);
    }
    auto field = make<fieldExpr>();
    field->from_ = from;
    field->name_ = dot->identifier()->getText();
    return static_cast<Expr*>(field);
  }

  Expr* appendSuffix(Expr *from, RxParser::PostfixSuffixContext* suf) {
    if (auto args = suf->callArguments(); args) {
      auto call = make<callExpr>();
      call->callee_ = from;
      auto exprs = args->expression();
      for (auto expr : exprs) {
        call->args_.push_back(anyExpr(visit(expr)));
      }
      return static_cast<Expr*>(call);
    }
    if (auto dot = suf->dotSuffix(); dot) {
      return static_cast<Expr*>(appendDotSuffix(from, dot));
    }
    auto index = make<indexExpr>();
    index->from_ = from;
    index->index_ = anyExpr(visit(suf->expression()));
    return static_cast<Expr*>(index);
  }

  Expr* appendSuffices(Expr* value, const std::vector<RxParser::PostfixSuffixContext*> &sufs) {
    for (auto suf : sufs) {
      value = appendSuffix(value, suf);
    }
    return static_cast<Expr*>(value);
  }

public:

  virtual std::any visitCrate(RxParser::CrateContext *ctx) override;

  virtual std::any visitItem(RxParser::ItemContext *ctx) override;

  virtual std::any visitUseDeclaration(RxParser::UseDeclarationContext *ctx) override;

  virtual std::any visitUseTree(RxParser::UseTreeContext *ctx) override;

  virtual std::any visitUsePath(RxParser::UsePathContext *ctx) override;

  virtual std::any visitUsePathSegment(RxParser::UsePathSegmentContext *ctx) override;

  virtual std::any visitFunctionDefinition(RxParser::FunctionDefinitionContext *ctx) override;

  virtual std::any visitFunctionParameters(RxParser::FunctionParametersContext *ctx) override;

  virtual std::any visitSelfParam(RxParser::SelfParamContext *ctx) override;

  virtual std::any visitFunctionParam(RxParser::FunctionParamContext *ctx) override;

  virtual std::any visitStructDefinition(RxParser::StructDefinitionContext *ctx) override;

  virtual std::any visitStructField(RxParser::StructFieldContext *ctx) override;

  virtual std::any visitOuterAttribute(RxParser::OuterAttributeContext *ctx) override;

  virtual std::any visitDeriveName(RxParser::DeriveNameContext *ctx) override;

  virtual std::any visitConstantItem(RxParser::ConstantItemContext *ctx) override;

  virtual std::any visitInherentImpl(RxParser::InherentImplContext *ctx) override;

  virtual std::any visitAssociatedItem(RxParser::AssociatedItemContext *ctx) override;

  virtual std::any visitGenericParams(RxParser::GenericParamsContext *ctx) override;

  virtual std::any visitLifetimeParam(RxParser::LifetimeParamContext *ctx) override;

  virtual std::any visitLifetime(RxParser::LifetimeContext *ctx) override;

  virtual std::any visitLifetimeBounds(RxParser::LifetimeBoundsContext *ctx) override;

  virtual std::any visitTypeParamBounds(RxParser::TypeParamBoundsContext *ctx) override;

  virtual std::any visitWhereClause(RxParser::WhereClauseContext *ctx) override;

  virtual std::any visitWhereClauseItem(RxParser::WhereClauseItemContext *ctx) override;

  virtual std::any visitTypeRef(RxParser::TypeRefContext *ctx) override;

  virtual std::any visitReferenceType(RxParser::ReferenceTypeContext *ctx) override;

  virtual std::any visitArrayType(RxParser::ArrayTypeContext *ctx) override;

  virtual std::any visitTypePath(RxParser::TypePathContext *ctx) override;

  virtual std::any visitTypePathSegment(RxParser::TypePathSegmentContext *ctx) override;

  virtual std::any visitPathInExpression(RxParser::PathInExpressionContext *ctx) override;

  virtual std::any visitPathExprSegment(RxParser::PathExprSegmentContext *ctx) override;

  virtual std::any visitPathIdentSegment(RxParser::PathIdentSegmentContext *ctx) override;

  virtual std::any visitGenericArgs(RxParser::GenericArgsContext *ctx) override;

  virtual std::any visitGenericArg(RxParser::GenericArgContext *ctx) override;

  virtual std::any visitGenericClose(RxParser::GenericCloseContext *ctx) override;

  virtual std::any visitClosedCastType(RxParser::ClosedCastTypeContext *ctx) override;

  virtual std::any visitConstValue(RxParser::ConstValueContext *ctx) override;

  virtual std::any visitMagnitude(RxParser::MagnitudeContext *ctx) override;

  virtual std::any visitIdentifierBinding(RxParser::IdentifierBindingContext *ctx) override;

  virtual std::any visitLetStatement(RxParser::LetStatementContext *ctx) override;

  virtual std::any visitBlockExpression(RxParser::BlockExpressionContext *ctx) override;

  virtual std::any visitStatement(RxParser::StatementContext *ctx) override;

  virtual std::any visitExpressionWithBlock(RxParser::ExpressionWithBlockContext *ctx) override;

  virtual std::any visitIfExpression(RxParser::IfExpressionContext *ctx) override;

  virtual std::any visitExpression(RxParser::ExpressionContext *ctx) override;

  virtual std::any visitAssignmentExpression(RxParser::AssignmentExpressionContext *ctx) override;

  virtual std::any visitLogicalOrExpression(RxParser::LogicalOrExpressionContext *ctx) override;

  virtual std::any visitLogicalAndExpression(RxParser::LogicalAndExpressionContext *ctx) override;

  virtual std::any visitComparisonExpression(RxParser::ComparisonExpressionContext *ctx) override;

  virtual std::any visitBitOrExpression(RxParser::BitOrExpressionContext *ctx) override;

  virtual std::any visitClosedBitOrExpression(RxParser::ClosedBitOrExpressionContext *ctx) override;

  virtual std::any visitBitXorExpression(RxParser::BitXorExpressionContext *ctx) override;

  virtual std::any visitClosedBitXorExpression(RxParser::ClosedBitXorExpressionContext *ctx) override;

  virtual std::any visitBitAndExpression(RxParser::BitAndExpressionContext *ctx) override;

  virtual std::any visitClosedBitAndExpression(RxParser::ClosedBitAndExpressionContext *ctx) override;

  virtual std::any visitShiftExpression(RxParser::ShiftExpressionContext *ctx) override;

  virtual std::any visitClosedShiftExpression(RxParser::ClosedShiftExpressionContext *ctx) override;

  virtual std::any visitAdditiveExpression(RxParser::AdditiveExpressionContext *ctx) override;

  virtual std::any visitClosedAdditiveExpression(RxParser::ClosedAdditiveExpressionContext *ctx) override;

  virtual std::any visitMultiplicativeExpression(RxParser::MultiplicativeExpressionContext *ctx) override;

  virtual std::any visitClosedMultiplicativeExpression(RxParser::ClosedMultiplicativeExpressionContext *ctx) override;

  virtual std::any visitCastExpression(RxParser::CastExpressionContext *ctx) override;

  virtual std::any visitClosedCastExpression(RxParser::ClosedCastExpressionContext *ctx) override;

  virtual std::any visitUnaryExpression(RxParser::UnaryExpressionContext *ctx) override;

  virtual std::any visitPostfixExpression(RxParser::PostfixExpressionContext *ctx) override;

  virtual std::any visitConditionExpression(RxParser::ConditionExpressionContext *ctx) override;

  virtual std::any visitConditionAssignmentExpression(RxParser::ConditionAssignmentExpressionContext *ctx) override;

  virtual std::any visitConditionLogicalOrExpression(RxParser::ConditionLogicalOrExpressionContext *ctx) override;

  virtual std::any visitConditionLogicalAndExpression(RxParser::ConditionLogicalAndExpressionContext *ctx) override;

  virtual std::any visitConditionComparisonExpression(RxParser::ConditionComparisonExpressionContext *ctx) override;

  virtual std::any visitConditionBitOrExpression(RxParser::ConditionBitOrExpressionContext *ctx) override;

  virtual std::any visitConditionClosedBitOrExpression(RxParser::ConditionClosedBitOrExpressionContext *ctx) override;

  virtual std::any visitConditionBitXorExpression(RxParser::ConditionBitXorExpressionContext *ctx) override;

  virtual std::any visitConditionClosedBitXorExpression(RxParser::ConditionClosedBitXorExpressionContext *ctx) override;

  virtual std::any visitConditionBitAndExpression(RxParser::ConditionBitAndExpressionContext *ctx) override;

  virtual std::any visitConditionClosedBitAndExpression(RxParser::ConditionClosedBitAndExpressionContext *ctx) override;

  virtual std::any visitConditionShiftExpression(RxParser::ConditionShiftExpressionContext *ctx) override;

  virtual std::any visitConditionClosedShiftExpression(RxParser::ConditionClosedShiftExpressionContext *ctx) override;

  virtual std::any visitConditionAdditiveExpression(RxParser::ConditionAdditiveExpressionContext *ctx) override;

  virtual std::any visitConditionClosedAdditiveExpression(RxParser::ConditionClosedAdditiveExpressionContext *ctx) override;

  virtual std::any visitConditionMultiplicativeExpression(RxParser::ConditionMultiplicativeExpressionContext *ctx) override;

  virtual std::any visitConditionClosedMultiplicativeExpression(RxParser::ConditionClosedMultiplicativeExpressionContext *ctx) override;

  virtual std::any visitConditionCastExpression(RxParser::ConditionCastExpressionContext *ctx) override;

  virtual std::any visitConditionClosedCastExpression(RxParser::ConditionClosedCastExpressionContext *ctx) override;

  virtual std::any visitConditionUnaryExpression(RxParser::ConditionUnaryExpressionContext *ctx) override;

  virtual std::any visitConditionPostfixExpression(RxParser::ConditionPostfixExpressionContext *ctx) override;

  virtual std::any visitConditionBreakExpression(RxParser::ConditionBreakExpressionContext *ctx) override;

  virtual std::any visitConditionBreakAssignmentExpression(RxParser::ConditionBreakAssignmentExpressionContext *ctx) override;

  virtual std::any visitConditionBreakLogicalOrExpression(RxParser::ConditionBreakLogicalOrExpressionContext *ctx) override;

  virtual std::any visitConditionBreakLogicalAndExpression(RxParser::ConditionBreakLogicalAndExpressionContext *ctx) override;

  virtual std::any visitConditionBreakComparisonExpression(RxParser::ConditionBreakComparisonExpressionContext *ctx) override;

  virtual std::any visitConditionBreakBitOrExpression(RxParser::ConditionBreakBitOrExpressionContext *ctx) override;

  virtual std::any visitConditionBreakClosedBitOrExpression(RxParser::ConditionBreakClosedBitOrExpressionContext *ctx) override;

  virtual std::any visitConditionBreakBitXorExpression(RxParser::ConditionBreakBitXorExpressionContext *ctx) override;

  virtual std::any visitConditionBreakClosedBitXorExpression(RxParser::ConditionBreakClosedBitXorExpressionContext *ctx) override;

  virtual std::any visitConditionBreakBitAndExpression(RxParser::ConditionBreakBitAndExpressionContext *ctx) override;

  virtual std::any visitConditionBreakClosedBitAndExpression(RxParser::ConditionBreakClosedBitAndExpressionContext *ctx) override;

  virtual std::any visitConditionBreakShiftExpression(RxParser::ConditionBreakShiftExpressionContext *ctx) override;

  virtual std::any visitConditionBreakClosedShiftExpression(RxParser::ConditionBreakClosedShiftExpressionContext *ctx) override;

  virtual std::any visitConditionBreakAdditiveExpression(RxParser::ConditionBreakAdditiveExpressionContext *ctx) override;

  virtual std::any visitConditionBreakClosedAdditiveExpression(RxParser::ConditionBreakClosedAdditiveExpressionContext *ctx) override;

  virtual std::any visitConditionBreakMultiplicativeExpression(RxParser::ConditionBreakMultiplicativeExpressionContext *ctx) override;

  virtual std::any visitConditionBreakClosedMultiplicativeExpression(RxParser::ConditionBreakClosedMultiplicativeExpressionContext *ctx) override;

  virtual std::any visitConditionBreakCastExpression(RxParser::ConditionBreakCastExpressionContext *ctx) override;

  virtual std::any visitConditionBreakClosedCastExpression(RxParser::ConditionBreakClosedCastExpressionContext *ctx) override;

  virtual std::any visitConditionBreakUnaryExpression(RxParser::ConditionBreakUnaryExpressionContext *ctx) override;

  virtual std::any visitConditionBreakPostfixExpression(RxParser::ConditionBreakPostfixExpressionContext *ctx) override;

  virtual std::any visitStatementExpression(RxParser::StatementExpressionContext *ctx) override;

  virtual std::any visitStatementAssignmentExpression(RxParser::StatementAssignmentExpressionContext *ctx) override;

  virtual std::any visitStatementLogicalOrExpression(RxParser::StatementLogicalOrExpressionContext *ctx) override;

  virtual std::any visitStatementLogicalAndExpression(RxParser::StatementLogicalAndExpressionContext *ctx) override;

  virtual std::any visitStatementComparisonExpression(RxParser::StatementComparisonExpressionContext *ctx) override;

  virtual std::any visitStatementBitOrExpression(RxParser::StatementBitOrExpressionContext *ctx) override;

  virtual std::any visitStatementClosedBitOrExpression(RxParser::StatementClosedBitOrExpressionContext *ctx) override;

  virtual std::any visitStatementBitXorExpression(RxParser::StatementBitXorExpressionContext *ctx) override;

  virtual std::any visitStatementClosedBitXorExpression(RxParser::StatementClosedBitXorExpressionContext *ctx) override;

  virtual std::any visitStatementBitAndExpression(RxParser::StatementBitAndExpressionContext *ctx) override;

  virtual std::any visitStatementClosedBitAndExpression(RxParser::StatementClosedBitAndExpressionContext *ctx) override;

  virtual std::any visitStatementShiftExpression(RxParser::StatementShiftExpressionContext *ctx) override;

  virtual std::any visitStatementClosedShiftExpression(RxParser::StatementClosedShiftExpressionContext *ctx) override;

  virtual std::any visitStatementAdditiveExpression(RxParser::StatementAdditiveExpressionContext *ctx) override;

  virtual std::any visitStatementClosedAdditiveExpression(RxParser::StatementClosedAdditiveExpressionContext *ctx) override;

  virtual std::any visitStatementMultiplicativeExpression(RxParser::StatementMultiplicativeExpressionContext *ctx) override;

  virtual std::any visitStatementClosedMultiplicativeExpression(RxParser::StatementClosedMultiplicativeExpressionContext *ctx) override;

  virtual std::any visitStatementCastExpression(RxParser::StatementCastExpressionContext *ctx) override;

  virtual std::any visitStatementClosedCastExpression(RxParser::StatementClosedCastExpressionContext *ctx) override;

  virtual std::any visitStatementUnaryExpression(RxParser::StatementUnaryExpressionContext *ctx) override;

  virtual std::any visitStatementPostfixExpression(RxParser::StatementPostfixExpressionContext *ctx) override;

  virtual std::any visitPrimaryExpression(RxParser::PrimaryExpressionContext *ctx) override;

  virtual std::any visitNonBlockPrimary(RxParser::NonBlockPrimaryContext *ctx) override;

  virtual std::any visitConditionPrimary(RxParser::ConditionPrimaryContext *ctx) override;

  virtual std::any visitConditionPrimaryWithoutBareBlock(RxParser::ConditionPrimaryWithoutBareBlockContext *ctx) override;

  virtual std::any visitLiteralExpression(RxParser::LiteralExpressionContext *ctx) override;

  virtual std::any visitStructExprFields(RxParser::StructExprFieldsContext *ctx) override;

  virtual std::any visitStructExprField(RxParser::StructExprFieldContext *ctx) override;

  virtual std::any visitArrayExpression(RxParser::ArrayExpressionContext *ctx) override;

  virtual std::any visitPostfixSuffix(RxParser::PostfixSuffixContext *ctx) override;

  virtual std::any visitDotSuffix(RxParser::DotSuffixContext *ctx) override;

  virtual std::any visitCallArguments(RxParser::CallArgumentsContext *ctx) override;

  virtual std::any visitUnaryOperator(RxParser::UnaryOperatorContext *ctx) override;

  virtual std::any visitMultiplicativeOperator(RxParser::MultiplicativeOperatorContext *ctx) override;

  virtual std::any visitAdditiveOperator(RxParser::AdditiveOperatorContext *ctx) override;

  virtual std::any visitShiftRight(RxParser::ShiftRightContext *ctx) override;

  virtual std::any visitComparisonExceptLt(RxParser::ComparisonExceptLtContext *ctx) override;

  virtual std::any visitAssignmentOperator(RxParser::AssignmentOperatorContext *ctx) override;

  virtual std::any visitEqualsSign(RxParser::EqualsSignContext *ctx) override;

  virtual std::any visitIdentifier(RxParser::IdentifierContext *ctx) override;

};