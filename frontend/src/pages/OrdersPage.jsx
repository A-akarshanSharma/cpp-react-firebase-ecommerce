import { Link } from 'react-router-dom'
import { api } from '../services/api'
import { usePagedResource } from '../hooks/usePagedResource'
import LoadMore from '../components/common/LoadMore'
import { EmptyState, ErrorState, Loader, PageHeading, StatusBadge } from '../components/common/UI'
import OrderDetails from '../components/orders/OrderDetails'
import { date, money } from '../utils/format'
export default function OrdersPage() {
  const resource = usePagedResource(api.getMyOrders)
  const { data, loading, error, reload } = resource
  return (
    <div className="container section">
      <PageHeading eyebrow="YOUR STUDIO STORY" title="Your orders">
        All your good finds, in one place.
      </PageHeading>
      {loading ? (
        <Loader />
      ) : error ? (
        <ErrorState message={error} retry={reload} />
      ) : !data.length ? (
        <EmptyState title="Your first find is waiting" to="/shop">
          You haven’t placed an order yet.
        </EmptyState>
      ) : (
        <div className="order-list">
          {[...data]
            .sort((a, b) => b.createdAt - a.createdAt)
            .map((o) => (
              <details className="order-card" key={o.id}>
                <summary>
                  <div>
                    <strong>Order #{o.id}</strong>
                    <p className="muted">{date(o.createdAt)}</p>
                  </div>
                  <StatusBadge status={o.status} />
                  <strong>{money(o.total)}</strong>
                  <span className="order-expand">
                    View details <span>+</span>
                  </span>
                </summary>
                <OrderDetails order={o} />
                <Link className="btn secondary" to={`/orders/${o.id}`}>
                  Manage order
                </Link>
              </details>
            ))}
        </div>
      )}
      <LoadMore resource={resource} />
    </div>
  )
}
