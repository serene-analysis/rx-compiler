1. 整段AI生成指的是连续超过300行（不含空行）的代码端到端生成。在本次强调之后再出现被确认使用AI生成的，将会按照原来的规则执行。我们验证整段AI生成的方法包括但不限于OpenAI、Claude、DeepSeek的代码水印检测。
2. CI/CD流程是一个必须的项目，从下次Review开始，所有的代码都需要提供 CI/CD 的记录，否则影响Code Review结果。
3. 下一个检查节点为IR，注意Semantic Checker的强度不低，没有完成Semantic Checker的同学请尽快完成。