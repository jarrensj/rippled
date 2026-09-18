#include <xrpl/tx/ApplyContext.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl {

ApplyContext::ApplyContext(
    ServiceRegistry& registry,
    OpenView& base,
    std::optional<uint256 const> const& parentBatchId,
    STTx const& tx,
    TER preclaimResult,
    XRPAmount baseFee,
    ApplyFlags flags,
    beast::Journal journal)
    : registry(registry)
    , tx(tx)
    , preclaimResult(preclaimResult)
    , baseFee(baseFee)
    , journal(journal)
    , base_(base)
    , flags_(flags)
    , parentBatchId_(parentBatchId)
{
    XRPL_ASSERT(
        parentBatchId.has_value() == ((flags_ & TapBatch) == TapBatch),
        "Parent Batch ID should be set if batch apply flag is set");
    view_.emplace(&base_, flags_);
}

void
ApplyContext::discard()
{
    view_.emplace(&base_, flags_);

    // The books were created by the changes being discarded.
    orderBooks_.clear();
}

std::optional<TxMeta>
ApplyContext::apply(TER ter)
{
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) view_ emplaced in constructor
    auto metadata =
        view_->apply(base_, tx, ter, parentBatchId_, (flags_ & TapDryRun) != 0u, journal);

    // For an ordinary transaction base_ is the ledger view, so the books are
    // real once the changes land there. A batch inner's base_ is the
    // per-transaction batch view, which the batch may still discard, so its
    // books wait for takeOrderBooks().
    if ((flags_ & TapBatch) == TapNone)
    {
        auto& orderBookDB = registry.get().getOrderBookDB();
        for (auto const& book : orderBooks_)
            orderBookDB.addOrderBook(book);
        orderBooks_.clear();
    }

    return metadata;
}

void
ApplyContext::addOrderBook(Book const& book)
{
    if ((flags_ & TapDryRun) == TapNone)
        orderBooks_.push_back(book);
}

std::vector<Book>
ApplyContext::takeOrderBooks()
{
    auto books = std::move(orderBooks_);
    orderBooks_.clear();
    return books;
}

std::size_t
ApplyContext::size()
{
    return view_->size();  // NOLINT(bugprone-unchecked-optional-access)
}

void
ApplyContext::visit(
    std::function<void(uint256 const&, bool, SLE::const_ref, SLE::const_ref)> const& func)
{
    view_->visit(base_, func);  // NOLINT(bugprone-unchecked-optional-access)
}

}  // namespace xrpl
