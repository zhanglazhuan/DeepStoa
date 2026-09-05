import { useState, useEffect } from 'react'
import { Link } from 'react-router-dom'
import { useAuth } from '../contexts/AuthContext'

interface Order {
  id: number
  status: string
  quantity: number
  created_at: string
}

const statusColors: Record<string, string> = {
  pending: 'bg-yellow-100 text-yellow-800',
  confirmed: 'bg-blue-100 text-blue-800',
  producing: 'bg-purple-100 text-purple-800',
  shipped: 'bg-green-100 text-green-800',
  cancelled: 'bg-red-100 text-red-800',
}

export default function AccountPage() {
  const { user } = useAuth()
  const [orders, setOrders] = useState<Order[]>([])
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    fetch('/api/orders/')
      .then((res) => res.json())
      .then(setOrders)
      .finally(() => setLoading(false))
  }, [])

  return (
    <div className="mx-auto max-w-3xl px-6 py-16 lg:px-12">
      <div className="mb-12">
        <div className="flex items-center gap-4">
          <div className="flex h-16 w-16 items-center justify-center rounded-full bg-neutral-200 text-xl font-medium text-neutral-600">
            {user?.first_name?.[0]}{user?.last_name?.[0]}
          </div>
          <div>
            <h1 className="text-2xl font-semibold text-neutral-900">
              {user?.first_name} {user?.last_name}
            </h1>
            <p className="text-neutral-500">{user?.email}</p>
          </div>
        </div>
      </div>

      <h2 className="mb-6 text-xl font-semibold text-neutral-900">Your Orders</h2>

      {loading ? (
        <p className="text-neutral-500">Loading...</p>
      ) : orders.length === 0 ? (
        <div className="rounded-lg border border-dashed border-neutral-300 px-6 py-12 text-center">
          <p className="text-neutral-500">No orders yet.</p>
          <Link
            to="/pre-order"
            className="mt-3 inline-block text-sm font-medium text-neutral-900 underline"
          >
            Place your first pre-order
          </Link>
        </div>
      ) : (
        <div className="space-y-3">
          {orders.map((order) => (
            <Link
              key={order.id}
              to={`/account/orders/${order.id}`}
              className="block rounded-lg border border-neutral-200 p-4 transition-colors hover:border-neutral-400"
            >
              <div className="flex items-center justify-between">
                <div>
                  <p className="font-medium text-neutral-900">Order #{order.id}</p>
                  <p className="text-sm text-neutral-500">
                    {new Date(order.created_at).toLocaleDateString()}
                  </p>
                </div>
                <div className="text-right">
                  <span
                    className={`inline-block rounded-full px-3 py-1 text-xs font-medium ${
                      statusColors[order.status] || ''
                    }`}
                  >
                    {order.status.charAt(0).toUpperCase() + order.status.slice(1)}
                  </span>
                  <p className="mt-1 text-sm text-neutral-500">Qty: {order.quantity}</p>
                </div>
              </div>
            </Link>
          ))}
        </div>
      )}
    </div>
  )
}
